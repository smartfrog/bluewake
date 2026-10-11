// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-13 disc probe.
//
// synthetic: the pinned dvd.c, compiled byte-identical through dvd_seam.h,
// exercised on an in-memory image (no file, no game data).
// real: the same pinned layer on a real image, cross-checked against an
// independent FST oracle and direct reads, then measured for throughput.
// Real mode writes its report both to stdout and to the given report path,
// so the Vita3K guest can deliver evidence through ux0 when console output
// is not captured.
#include "dvd_fake_backend.h"

#include "core/cpu.h"
#include "gxruntime/dvd.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SYNTH_IMAGE_BYTES 0x1000u
#define SYNTH_FST_OFFSET 0x440u
#define SYNTH_FST_BYTES 93u
#define SYNTH_GUEST_OFFSET 0x100000u
#define SYNTH_UNCACHED_OFFSET 0x200000u
#define SYNTH_PAST_EOF_OFFSET 0x300000u
#define SYNTH_PAST_EOF_DISC 0xFF0u
#define SYNTH_PAST_EOF_LENGTH 0x40u

#define REAL_GUEST_OFFSET 0x100000u
#define REAL_SAMPLE_BYTES 0x10000u
#define REAL_TAIL_BYTES 0x1000u
#define REAL_WINDOW_BYTES 0x1000000u
#define REAL_READ_CAP_BYTES (256ull * 1024ull * 1024ull)

static const char* g_label = "PROBE";
static FILE* g_report;
static unsigned failures;

static void out(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    if (g_report != NULL) {
        va_start(arguments, format);
        vfprintf(g_report, format, arguments);
        va_end(arguments);
    }
}

static void check(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "%s FAIL: %s\n", g_label, message);
        if (g_report != NULL)
            fprintf(g_report, "%s FAIL: %s\n", g_label, message);
        failures++;
    }
}

static void put_be32(unsigned char* p, uint32_t value) {
    p[0] = (unsigned char)(value >> 24);
    p[1] = (unsigned char)(value >> 16);
    p[2] = (unsigned char)(value >> 8);
    p[3] = (unsigned char)value;
}

static uint32_t be32(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static unsigned char pattern(unsigned long long offset) {
    return (unsigned char)((offset * 7u + 3u) & 0xFFu);
}

// ---------------------------------------------------------------------------
// Synthetic image
// ---------------------------------------------------------------------------
static void put_fst_entry(unsigned char* fst, unsigned index, uint32_t word0, uint32_t word1,
                          uint32_t word2) {
    unsigned char* entry = fst + (size_t)index * 12u;
    put_be32(entry, word0);
    put_be32(entry + 4u, word1);
    put_be32(entry + 8u, word2);
}

// One root file, one subdirectory with two files; the string table starts at
// fst + 5 * 12.
static void build_synthetic_image(void) {
    static const char strings[] = "\0boot.bin\0res\0asset.bin\0leaf.bin";
    bw_fake_install(SYNTH_IMAGE_BYTES);
    unsigned char* image = bw_fake_image.bytes;
    for (unsigned long long i = 0; i < SYNTH_IMAGE_BYTES; ++i)
        image[i] = pattern(i);
    put_be32(image + 0x1Cu, 0xC2339F3Du);
    put_be32(image + 0x424u, SYNTH_FST_OFFSET);
    put_be32(image + 0x428u, SYNTH_FST_BYTES);
    unsigned char* fst = image + SYNTH_FST_OFFSET;
    put_fst_entry(fst, 0u, 0x01000000u, 0u, 5u);
    put_fst_entry(fst, 1u, 1u, 0x500u, 0x30u);
    put_fst_entry(fst, 2u, 0x01000000u | 10u, 0u, 5u);
    put_fst_entry(fst, 3u, 14u, 0x600u, 0x100u);
    put_fst_entry(fst, 4u, 24u, 0x800u, 0x40u);
    memcpy(fst + 60u, strings, sizeof strings);
}

typedef struct {
    const char* path;
    int entry;
    uint32_t start;
    uint32_t length;
} ResolveCase;

static const ResolveCase RESOLVE_CASES[] = {
    {"boot.bin", 1, 0x500u, 0x30u},
    {"res", 2, 0u, 0u},
    {"res/asset.bin", 3, 0x600u, 0x100u},
    {"res/leaf.bin", 4, 0x800u, 0x40u},
    {"BOOT.BIN", 1, 0x500u, 0x30u},
    {"/res/../boot.bin", 1, 0x500u, 0x30u},
    {"res/", 2, 0u, 0u},
    {"missing", -1, 0u, 0u},
    {"res/missing", -1, 0u, 0u},
    {"asset.bin", -1, 0u, 0u},
};

typedef struct {
    const char* path;
    uint32_t start;
    uint32_t length;
    uint32_t guest;
} GuestCase;

static const GuestCase GUEST_CASES[] = {
    {"boot.bin", 0x500u, 0x30u, GC_RAM_BASE + SYNTH_GUEST_OFFSET},
    {"res/asset.bin", 0x600u, 0x100u, GC_RAM_BASE + SYNTH_GUEST_OFFSET + 0x1000u},
    {"res/leaf.bin", 0x800u, 0x40u, GC_RAM_UNCACHED + SYNTH_UNCACHED_OFFSET},
};

static int run_synthetic(void) {
    g_label = "SYNTHETIC";
    build_synthetic_image();
    out("backend %s\n", bw_dvd_io_backend_name());
    out("image bytes=%u fst=%u entries=5\n", SYNTH_IMAGE_BYTES, SYNTH_FST_OFFSET);
    if (!dvd_open_image("fake.iso")) {
        fprintf(stderr, "SYNTHETIC FAIL: pinned dvd_open_image rejected the image\n");
        return 1;
    }
    CPUState* cpu = (CPUState*)calloc(1u, sizeof(CPUState));
    if (cpu == NULL || !cpu_init(cpu)) {
        fprintf(stderr, "SYNTHETIC FAIL: CPUState allocation failed\n");
        free(cpu);
        return 1;
    }

    for (size_t i = 0; i < sizeof RESOLVE_CASES / sizeof RESOLVE_CASES[0]; ++i) {
        const ResolveCase* test = &RESOLVE_CASES[i];
        const s32 entry = dvd_path_to_entrynum(test->path);
        uint32_t start = 0u;
        uint32_t length = 0u;
        const bool is_file = entry >= 0 && dvd_entry_info(entry, &start, &length);
        out("resolve %s %d %u %u\n", test->path, (int)entry, is_file ? start : 0u,
            is_file ? length : 0u);
        check((int)entry == test->entry, "entry mismatch");
        if (test->entry >= 0 && is_file)
            check(start == test->start && length == test->length, "file info mismatch");
    }

    for (size_t i = 0; i < sizeof GUEST_CASES / sizeof GUEST_CASES[0]; ++i) {
        const GuestCase* test = &GUEST_CASES[i];
        dvd_read_to_guest(cpu, test->guest, test->start, test->length);
        const u32 ram_offset = test->guest >= GC_RAM_UNCACHED ? test->guest - GC_RAM_UNCACHED
                                                              : test->guest - GC_RAM_BASE;
        const bool ok = memcmp(cpu->ram + ram_offset, bw_fake_image.bytes + test->start,
                               test->length) == 0;
        const char* label = test->guest >= GC_RAM_UNCACHED ? "guest-uncached" : "guest";
        out("%s %s %s %u\n", label, test->path, ok ? "ok" : "mismatch", test->length);
        check(ok, "guest copy differs from the image");
    }

    dvd_read_to_guest(cpu, GC_RAM_BASE + SYNTH_PAST_EOF_OFFSET, SYNTH_PAST_EOF_DISC,
                      SYNTH_PAST_EOF_LENGTH);
    const u8* dst = cpu->ram + SYNTH_PAST_EOF_OFFSET;
    u32 prefix = 0u;
    while (prefix < SYNTH_PAST_EOF_LENGTH &&
           dst[prefix] == pattern(SYNTH_PAST_EOF_DISC + prefix))
        prefix++;
    u32 zeros = 0u;
    while (prefix + zeros < SYNTH_PAST_EOF_LENGTH && dst[prefix + zeros] == 0u)
        zeros++;
    out("guest-past-eof prefix=%u zeros=%u\n", prefix, zeros);
    check(prefix == SYNTH_IMAGE_BYTES - SYNTH_PAST_EOF_DISC, "past-EOF prefix mismatch");
    check(zeros == SYNTH_PAST_EOF_LENGTH - prefix, "past-EOF zero fill mismatch");

    BwDvdIoStats stats;
    bw_dvd_io_stats(&stats);
    out("stats total=%llu chunk=%llu staging=%llu\n",
        (unsigned long long)stats.total_read_bytes,
        (unsigned long long)stats.max_chunk_bytes,
        (unsigned long long)stats.staging_peak_bytes);
    check(stats.staging_peak_bytes == 0u, "staging bytes recorded");

    dvd_close_image();
    cpu_free(cpu);
    free(cpu);
    bw_fake_release();
    if (failures != 0u)
        return 1;
    out("SYNTHETIC PASS\n");
    return 0;
}

// ---------------------------------------------------------------------------
// Independent FST oracle (plain stdio; never the pinned layer)
// ---------------------------------------------------------------------------
typedef struct {
    FILE* image;
    unsigned char* fst;
    uint32_t entries;
    const char* strings;
} Oracle;

static uint32_t oracle_stroff(const Oracle* oracle, uint32_t i) {
    return be32(oracle->fst + (size_t)i * 12u) & 0x00FFFFFFu;
}
static bool oracle_is_dir(const Oracle* oracle, uint32_t i) {
    return (be32(oracle->fst + (size_t)i * 12u) & 0xFF000000u) != 0u;
}
static uint32_t oracle_parent(const Oracle* oracle, uint32_t i) {
    return be32(oracle->fst + (size_t)i * 12u + 4u);
}
static uint32_t oracle_next(const Oracle* oracle, uint32_t i) {
    return be32(oracle->fst + (size_t)i * 12u + 8u);
}
static uint32_t oracle_start(const Oracle* oracle, uint32_t i) {
    return be32(oracle->fst + (size_t)i * 12u + 4u);
}
static uint32_t oracle_length(const Oracle* oracle, uint32_t i) {
    return be32(oracle->fst + (size_t)i * 12u + 8u);
}
static const char* oracle_name(const Oracle* oracle, uint32_t i) {
    return oracle->strings + oracle_stroff(oracle, i);
}

static bool oracle_load(const char* path, Oracle* oracle) {
    memset(oracle, 0, sizeof *oracle);
    FILE* image = fopen(path, "rb");
    if (image == NULL)
        return false;
    unsigned char header[0x440];
    if (fread(header, 1, sizeof header, image) != sizeof header ||
        be32(header + 0x1Cu) != 0xC2339F3Du) {
        fclose(image);
        return false;
    }
    const uint32_t fst_off = be32(header + 0x424u);
    const uint32_t fst_size = be32(header + 0x428u);
    if (fst_size < 12u || fst_size > 0x4000000u) {
        fclose(image);
        return false;
    }
    unsigned char* fst = (unsigned char*)malloc(fst_size);
    if (fst == NULL) {
        fclose(image);
        return false;
    }
    if (fseeko(image, (off_t)fst_off, SEEK_SET) != 0 || fread(fst, 1, fst_size, image) != fst_size) {
        free(fst);
        fclose(image);
        return false;
    }
    const uint32_t entries = be32(fst + 8u);
    if ((uint64_t)entries * 12u > fst_size) {
        free(fst);
        fclose(image);
        return false;
    }
    oracle->image = image;
    oracle->fst = fst;
    oracle->entries = entries;
    oracle->strings = (const char*)(fst + (size_t)entries * 12u);
    return true;
}

static void oracle_release(Oracle* oracle) {
    if (oracle->image != NULL)
        fclose(oracle->image);
    free(oracle->fst);
    memset(oracle, 0, sizeof *oracle);
}

// The same walk the DVD SDK specifies, written independently of the pin.
static s32 oracle_find(const Oracle* oracle, const char* path) {
    uint32_t dir = 0u;
    while (1) {
        if (*path == '\0')
            return (s32)dir;
        if (*path == '/') {
            dir = 0u;
            path++;
            continue;
        }
        if (path[0] == '.' && path[1] == '/') {
            path += 2;
            continue;
        }
        if (path[0] == '.' && path[1] == '.') {
            if (path[2] == '/') {
                dir = oracle_parent(oracle, dir);
                path += 3;
                continue;
            }
            if (path[2] == '\0')
                return (s32)oracle_parent(oracle, dir);
        }
        if (path[0] == '.' && path[1] == '\0')
            return (s32)dir;
        const char* end = path;
        while (*end != '\0' && *end != '/')
            end++;
        const bool want_dir = *end != '\0';
        const size_t length = (size_t)(end - path);
        bool found = false;
        uint32_t i = dir + 1u;
        while (i < oracle_next(oracle, dir)) {
            if (!oracle_is_dir(oracle, i) && want_dir) {
                i++;
                continue;
            }
            const char* name = oracle_name(oracle, i);
            if (strlen(name) == length && strncasecmp(name, path, length) == 0) {
                found = true;
                break;
            }
            i = oracle_is_dir(oracle, i) ? oracle_next(oracle, i) : i + 1u;
        }
        if (!found)
            return -1;
        if (!want_dir)
            return (s32)i;
        dir = i;
        path += length + 1u;
    }
}

static bool oracle_largest(const Oracle* oracle, uint32_t* entry, uint32_t* start,
                           uint32_t* length, const char** name) {
    *entry = 0u;
    *start = 0u;
    *length = 0u;
    *name = "";
    for (uint32_t i = 1u; i < oracle->entries; ++i) {
        if (oracle_is_dir(oracle, i))
            continue;
        const uint32_t candidate = oracle_length(oracle, i);
        if (candidate > *length) {
            *entry = i;
            *start = oracle_start(oracle, i);
            *length = candidate;
            *name = oracle_name(oracle, i);
        }
    }
    return *length > 0u;
}

static bool direct_read(const Oracle* oracle, uint32_t offset, void* dst, uint32_t length) {
    return fseeko(oracle->image, (off_t)offset, SEEK_SET) == 0 &&
           fread(dst, 1, length, oracle->image) == length;
}

static double seconds_between(const struct timespec* start, const struct timespec* end) {
    return (double)(end->tv_sec - start->tv_sec) + (double)(end->tv_nsec - start->tv_nsec) / 1e9;
}

static const char* REAL_PATHS[] = {"framework.str", "RELS.arc", "res/Menu/Menu1.dat",
                                   "res/Msg/bmgres.arc"};

static int finish_real(int status) {
    if (g_report != NULL) {
        fflush(g_report);
        fclose(g_report);
        g_report = NULL;
    }
    fflush(stdout);
    return status;
}

static int run_real(const char* image_path, const char* report_path, int extra_count,
                    char** extra_paths) {
    g_label = "REAL";
    g_report = fopen(report_path, "wb");
    if (g_report == NULL) {
        fprintf(stderr, "REAL FAIL: cannot open the report file: %s\n", report_path);
        return 2;
    }
    setvbuf(g_report, NULL, _IONBF, 0);
    Oracle oracle;
    if (!oracle_load(image_path, &oracle)) {
        fprintf(stderr, "REAL FAIL: cannot read the image header/FST\n");
        if (!dvd_open_image(image_path))
            fprintf(stderr, "REAL FAIL: pinned dvd_open_image rejected the image\n");
        return finish_real(2);
    }
    uint32_t largest_entry = 0u;
    uint32_t largest_start = 0u;
    uint32_t largest_length = 0u;
    const char* largest_name = "";
    if (!oracle_largest(&oracle, &largest_entry, &largest_start, &largest_length, &largest_name)) {
        fprintf(stderr, "REAL FAIL: the oracle found no file entry\n");
        oracle_release(&oracle);
        return finish_real(2);
    }
    out("oracle entries=%u files-largest=%s start=%u length=%u\n", oracle.entries, largest_name,
        largest_start, largest_length);
    out("backend %s\n", bw_dvd_io_backend_name());
    if (!dvd_open_image(image_path)) {
        fprintf(stderr, "REAL FAIL: pinned dvd_open_image rejected the image\n");
        oracle_release(&oracle);
        return finish_real(2);
    }
    CPUState* cpu = (CPUState*)calloc(1u, sizeof(CPUState));
    if (cpu == NULL || !cpu_init(cpu)) {
        fprintf(stderr, "REAL FAIL: CPUState allocation failed\n");
        free(cpu);
        oracle_release(&oracle);
        return finish_real(2);
    }

    const char** paths = extra_count > 0 ? (const char**)extra_paths : REAL_PATHS;
    const int path_count = extra_count > 0 ? extra_count
                                           : (int)(sizeof REAL_PATHS / sizeof REAL_PATHS[0]);
    unsigned char* sample = (unsigned char*)malloc(REAL_SAMPLE_BYTES);
    if (sample == NULL) {
        fprintf(stderr, "REAL FAIL: sample allocation failed\n");
        dvd_close_image();
        cpu_free(cpu);
        free(cpu);
        oracle_release(&oracle);
        return finish_real(2);
    }
    for (int p = 0; p < path_count; ++p) {
        const char* path = paths[p];
        const s32 entry = dvd_path_to_entrynum(path);
        uint32_t start = 0u;
        uint32_t length = 0u;
        const bool is_file = entry >= 0 && dvd_entry_info(entry, &start, &length);
        out("resolve %s %d %u %u\n", path, (int)entry, is_file ? start : 0u,
            is_file ? length : 0u);
        const s32 oracle_entry = oracle_find(&oracle, path);
        bool match = oracle_entry == entry;
        if (match && entry >= 0) {
            if (oracle_is_dir(&oracle, (uint32_t)entry))
                match = !is_file;
            else
                match = is_file && oracle_start(&oracle, (uint32_t)entry) == start &&
                        oracle_length(&oracle, (uint32_t)entry) == length;
        }
        out("pin-matches-oracle %s %s\n", path, match ? "ok" : "mismatch");
        check(match, "pin resolution differs from the oracle");
        if (!is_file)
            continue;
        const uint32_t sample_bytes = length < REAL_SAMPLE_BYTES ? length : REAL_SAMPLE_BYTES;
        dvd_read_to_guest(cpu, GC_RAM_BASE + REAL_GUEST_OFFSET, start, sample_bytes);
        const bool ok = direct_read(&oracle, start, sample, sample_bytes) &&
                        memcmp(cpu->ram + REAL_GUEST_OFFSET, sample, sample_bytes) == 0;
        out("guest %s %s %u\n", path, ok ? "ok" : "mismatch", sample_bytes);
        check(ok, "guest sample differs from the direct read");
    }

    {
        const uint32_t tail = largest_length < REAL_TAIL_BYTES ? largest_length : REAL_TAIL_BYTES;
        const uint32_t tail_offset = largest_start + largest_length - tail;
        dvd_read_to_guest(cpu, GC_RAM_BASE + REAL_GUEST_OFFSET + REAL_SAMPLE_BYTES, tail_offset,
                          tail);
        const bool ok = direct_read(&oracle, tail_offset, sample, tail) &&
                        memcmp(cpu->ram + REAL_GUEST_OFFSET + REAL_SAMPLE_BYTES, sample, tail) == 0;
        out("guest-tail %s %s %u\n", largest_name, ok ? "ok" : "mismatch", tail);
        check(ok, "largest-file tail differs from the direct read");
    }

    {
        const unsigned long long total = largest_length < REAL_READ_CAP_BYTES
                                             ? largest_length
                                             : REAL_READ_CAP_BYTES;
        struct timespec start_time;
        struct timespec end_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        unsigned long long read_bytes = 0u;
        while (read_bytes < total) {
            const uint32_t window = (uint32_t)(total - read_bytes < REAL_WINDOW_BYTES
                                                   ? total - read_bytes
                                                   : REAL_WINDOW_BYTES);
            dvd_read_to_guest(cpu, GC_RAM_BASE + REAL_GUEST_OFFSET,
                              largest_start + (uint32_t)read_bytes, window);
            read_bytes += window;
        }
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        const double seconds = seconds_between(&start_time, &end_time);
        out("throughput bytes=%llu seconds=%.3f mib_per_s=%.1f\n", read_bytes, seconds,
            seconds > 0.0 ? (double)read_bytes / 1048576.0 / seconds : 0.0);
    }

    BwDvdIoStats stats;
    bw_dvd_io_stats(&stats);
    out("stats total=%llu chunk=%llu staging=%llu\n",
        (unsigned long long)stats.total_read_bytes,
        (unsigned long long)stats.max_chunk_bytes,
        (unsigned long long)stats.staging_peak_bytes);
    check(stats.staging_peak_bytes == 0u, "staging bytes recorded");

    free(sample);
    dvd_close_image();
    cpu_free(cpu);
    free(cpu);
    oracle_release(&oracle);
    if (failures != 0u)
        return finish_real(1);
    out("REAL PASS\n");
    return finish_real(0);
}

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "synthetic") == 0)
        return run_synthetic();
    if (argc >= 4 && strcmp(argv[1], "real") == 0)
        return run_real(argv[2], argv[3], argc - 4, argv + 4);
    fprintf(stderr, "usage: dvd_probe synthetic\n"
                    "       dvd_probe real <image.iso> <report-path> [path...]\n");
    return 2;
}
