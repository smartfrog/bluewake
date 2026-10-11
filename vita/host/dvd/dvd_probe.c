// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-13 disc probe: the pinned dvd.c, compiled byte-identical through
// dvd_seam.h, exercised on a synthetic in-memory image. Prints one line per
// check for test_dvd_probe.py. No real file and no game data are involved.
#include "dvd_fake_backend.h"

#include "core/cpu.h"
#include "gxruntime/dvd.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYNTH_IMAGE_BYTES 0x1000u
#define SYNTH_FST_OFFSET 0x440u
#define SYNTH_FST_BYTES 93u
#define SYNTH_GUEST_OFFSET 0x100000u
#define SYNTH_UNCACHED_OFFSET 0x200000u
#define SYNTH_PAST_EOF_OFFSET 0x300000u
#define SYNTH_PAST_EOF_DISC 0xFF0u
#define SYNTH_PAST_EOF_LENGTH 0x40u

static unsigned failures;

static void check(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "SYNTHETIC FAIL: %s\n", message);
        failures++;
    }
}

static void put_be32(unsigned char* p, uint32_t value) {
    p[0] = (unsigned char)(value >> 24);
    p[1] = (unsigned char)(value >> 16);
    p[2] = (unsigned char)(value >> 8);
    p[3] = (unsigned char)value;
}

static unsigned char pattern(unsigned long long offset) {
    return (unsigned char)((offset * 7u + 3u) & 0xFFu);
}

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
    build_synthetic_image();
    printf("backend %s\n", bw_dvd_io_backend_name());
    printf("image bytes=%u fst=%u entries=5\n", SYNTH_IMAGE_BYTES, SYNTH_FST_OFFSET);
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
        printf("resolve %s %d %u %u\n", test->path, (int)entry, is_file ? start : 0u,
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
        printf("%s %s %s %u\n", label, test->path, ok ? "ok" : "mismatch", test->length);
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
    printf("guest-past-eof prefix=%u zeros=%u\n", prefix, zeros);
    check(prefix == SYNTH_IMAGE_BYTES - SYNTH_PAST_EOF_DISC, "past-EOF prefix mismatch");
    check(zeros == SYNTH_PAST_EOF_LENGTH - prefix, "past-EOF zero fill mismatch");

    BwDvdIoStats stats;
    bw_dvd_io_stats(&stats);
    printf("stats total=%llu chunk=%llu staging=%llu\n",
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
    printf("SYNTHETIC PASS\n");
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "synthetic") == 0)
        return run_synthetic();
    fprintf(stderr, "usage: dvd_probe synthetic\n");
    return 2;
}
