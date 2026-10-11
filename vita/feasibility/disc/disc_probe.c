// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-13 disc probe: exercise the pinned DVD layer against a real image.
//
// Reads only; the private image path is an argument. Prints machine-readable
// lines for test_disc_probe.py:
//   entries <N>                              (independent header/FST read)
//   resolve <path> <entry> <offset> <length> (pinned dvd_path_to_entrynum + dvd_entry_info)
//   digest <path> <fnv1a64> <n>              (first min(length, 4096) bytes via dvd_read_to_guest)
//   guest_ok <1|0>                           (guest copy byte-identical to a direct read)

#include "core/cpu.h"
#include "gxruntime/dvd.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROBE_DIGEST_BYTES 4096u

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t fnv1a64(const uint8_t* data, size_t size) {
    uint64_t value = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < size; ++i) {
        value ^= data[i];
        value *= 0x100000001B3ull;
    }
    return value;
}

// The pinned layer exposes no entry count, so the probe reads the header and
// FST itself for that one number; every resolution below goes through the pin.
static bool read_entry_count(const char* path, uint32_t* out) {
    FILE* f = fopen(path, "rb");
    if (f == NULL)
        return false;
    uint8_t header[0x440];
    if (fread(header, 1, sizeof header, f) != sizeof header) {
        fclose(f);
        return false;
    }
    const uint32_t fst_off = be32(header + 0x424);
    const uint32_t fst_size = be32(header + 0x428);
    if (fst_size < 12u || fst_size > 0x4000000u) {
        fclose(f);
        return false;
    }
    uint8_t* fst = (uint8_t*)malloc(fst_size);
    if (fst == NULL) {
        fclose(f);
        return false;
    }
    const bool ok = fseeko(f, (off_t)fst_off, SEEK_SET) == 0 &&
                    fread(fst, 1, fst_size, f) == fst_size;
    if (ok)
        *out = be32(fst + 8);
    free(fst);
    fclose(f);
    return ok;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: disc_probe <image.iso> <path>...\n");
        return 2;
    }
    uint32_t entries = 0;
    if (!read_entry_count(argv[1], &entries)) {
        fprintf(stderr, "cannot read image header/FST: %s\n", argv[1]);
        return 2;
    }
    printf("entries %u\n", entries);

    if (!dvd_open_image(argv[1])) {
        fprintf(stderr, "pinned dvd_open_image rejected: %s\n", argv[1]);
        return 2;
    }

    CPUState cpu;
    if (!cpu_init(&cpu)) {
        fprintf(stderr, "cpu_init failed\n");
        return 2;
    }
    FILE* direct = fopen(argv[1], "rb");
    if (direct == NULL) {
        fprintf(stderr, "cannot reopen image: %s\n", argv[1]);
        cpu_free(&cpu);
        return 2;
    }

    int guest_ok = 1;
    for (int i = 2; i < argc; ++i) {
        const char* path = argv[i];
        const s32 entry = dvd_path_to_entrynum(path);
        uint32_t start = 0;
        uint32_t length = 0;
        const bool is_file = entry >= 0 && dvd_entry_info(entry, &start, &length);
        printf("resolve %s %d %u %u\n", path, (int)entry,
               is_file ? start : 0u, is_file ? length : 0u);
        if (!is_file)
            continue;
        const uint32_t n = length < PROBE_DIGEST_BYTES ? length : PROBE_DIGEST_BYTES;
        uint8_t reference[PROBE_DIGEST_BYTES];
        if (n > 0u) {
            if (fseeko(direct, (off_t)start, SEEK_SET) != 0 ||
                fread(reference, 1, n, direct) != n) {
                fprintf(stderr, "direct read failed for %s\n", path);
                guest_ok = 0;
                continue;
            }
            dvd_read_to_guest(&cpu, GC_RAM_BASE + 0x100000u, start, n);
            if (memcmp(cpu.ram + 0x100000u, reference, n) != 0)
                guest_ok = 0;
        }
        printf("digest %s %016llx %u\n", path,
               (unsigned long long)fnv1a64(reference, n), n);
    }
    printf("guest_ok %d\n", guest_ok);

    fclose(direct);
    cpu_free(&cpu);
    dvd_close_image();
    return 0;
}
