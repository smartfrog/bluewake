// SPDX-License-Identifier: GPL-3.0-or-later
// Host contract tests for the disc I/O seam. The shared fake backend exercises
// the stdio-shaped shims the pinned dvd.c is redirected to: no real file, no
// pinned runtime, deterministic failure injection.
#include "dvd_io.h"
#include "dvd_fake_backend.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void reset(unsigned long long size) {
    bw_dvd_io_set_read_ahead(0u);
    bw_fake_install(size);
    for (unsigned long long i = 0; i < size; ++i)
        bw_fake_image.bytes[i] = (unsigned char)(i * 131u + 7u);
}

#define CHECK(condition, message)                          \
    do {                                                   \
        if (!(condition)) {                                \
            fprintf(stderr, "FAIL %s: %s\n", name, message); \
            return 1;                                      \
        }                                                  \
    } while (0)

static int case_open_close(const char* name) {
    reset(0u);
    FILE* stream = bw_dvd_fopen("fake.iso", "rb");
    CHECK(stream != NULL, "open of fake.iso failed");
    CHECK(bw_dvd_io_backend()->name != NULL, "backend has no name");
    CHECK(bw_dvd_fclose(stream) == 0, "close failed");
    CHECK(bw_dvd_fopen("missing.iso", "rb") == NULL, "open of missing.iso succeeded");
    CHECK(bw_dvd_fopen("fake.iso", "wb") == NULL, "write mode accepted");
    CHECK(bw_dvd_fopen("fake.iso", "r+b") == NULL, "update mode accepted");
    printf("PASS %s\n", name);
    return 0;
}

static int case_read_sequential(const char* name) {
    reset(4096u);
    FILE* stream = bw_dvd_fopen("fake.iso", "rb");
    CHECK(stream != NULL, "open failed");
    unsigned char first[256];
    unsigned char second[256];
    CHECK(bw_dvd_fread(first, 1u, sizeof first, stream) == sizeof first, "first read short");
    CHECK(memcmp(first, bw_fake_image.bytes, sizeof first) == 0, "first read bytes differ");
    CHECK(bw_dvd_fread(second, 1u, sizeof second, stream) == sizeof second, "second read short");
    CHECK(memcmp(second, bw_fake_image.bytes + sizeof first, sizeof second) == 0,
          "second read did not continue the position");
    unsigned char pair[8];
    CHECK(bw_dvd_fread(pair, 2u, 4u, stream) == 4u, "item read count differs");
    CHECK(memcmp(pair, bw_fake_image.bytes + 512u, sizeof pair) == 0, "item read bytes differ");
    CHECK(bw_dvd_fclose(stream) == 0, "close failed");
    printf("PASS %s\n", name);
    return 0;
}

static int case_short_read_eof(const char* name) {
    reset(4096u);
    FILE* stream = bw_dvd_fopen("fake.iso", "rb");
    CHECK(stream != NULL, "open failed");
    CHECK(bw_dvd_fseeko(stream, 4090, SEEK_SET) == 0, "seek failed");
    unsigned char tail[16];
    CHECK(bw_dvd_fread(tail, 1u, sizeof tail, stream) == 6u, "short read at EOF not reported");
    CHECK(memcmp(tail, bw_fake_image.bytes + 4090u, 6u) == 0, "tail bytes differ");
    CHECK(bw_dvd_fclose(stream) == 0, "close failed");
    printf("PASS %s\n", name);
    return 0;
}

static int case_read_past_eof(const char* name) {
    reset(64u);
    FILE* stream = bw_dvd_fopen("fake.iso", "rb");
    CHECK(stream != NULL, "open failed");
    CHECK(bw_dvd_fseeko(stream, 128, SEEK_SET) == 0, "seek past EOF failed");
    unsigned char byte = 0u;
    CHECK(bw_dvd_fread(&byte, 1u, 1u, stream) == 0u, "read past EOF returned data");
    CHECK(bw_dvd_fclose(stream) == 0, "close failed");
    printf("PASS %s\n", name);
    return 0;
}

static int case_read_failure(const char* name) {
    reset(4096u);
    bw_fake_image.read_fails = true;
    FILE* stream = bw_dvd_fopen("fake.iso", "rb");
    CHECK(stream != NULL, "open failed");
    unsigned char buffer[16];
    CHECK(bw_dvd_fread(buffer, 1u, sizeof buffer, stream) == 0u,
          "failed read reported bytes");
    CHECK(bw_dvd_fclose(stream) == 0, "close failed");
    printf("PASS %s\n", name);
    return 0;
}

static int case_seek_set_cur_end(const char* name) {
    reset(4096u);
    FILE* stream = bw_dvd_fopen("fake.iso", "rb");
    CHECK(stream != NULL, "open failed");
    CHECK(bw_dvd_fseeko(stream, 100, SEEK_SET) == 0, "SEEK_SET failed");
    unsigned char data[8];
    CHECK(bw_dvd_fread(data, 1u, sizeof data, stream) == sizeof data, "read after SEEK_SET short");
    CHECK(memcmp(data, bw_fake_image.bytes + 100u, sizeof data) == 0, "SEEK_SET bytes differ");
    CHECK(bw_dvd_fseeko(stream, -8, SEEK_CUR) == 0, "SEEK_CUR failed");
    CHECK(bw_dvd_fread(data, 1u, sizeof data, stream) == sizeof data, "read after SEEK_CUR short");
    CHECK(memcmp(data, bw_fake_image.bytes + 100u, sizeof data) == 0, "SEEK_CUR bytes differ");
    CHECK(bw_dvd_fseeko(stream, -4, SEEK_END) == 0, "SEEK_END failed");
    CHECK(bw_dvd_fread(data, 1u, sizeof data, stream) == 4u, "SEEK_END tail not short");
    CHECK(memcmp(data, bw_fake_image.bytes + 4092u, 4u) == 0, "SEEK_END bytes differ");
    CHECK(bw_dvd_fclose(stream) == 0, "close failed");
    printf("PASS %s\n", name);
    return 0;
}

static int case_seek_invalid(const char* name) {
    reset(4096u);
    FILE* stream = bw_dvd_fopen("fake.iso", "rb");
    CHECK(stream != NULL, "open failed");
    CHECK(bw_dvd_fseeko(stream, -1, SEEK_SET) == -1, "negative SEEK_SET accepted");
    CHECK(bw_dvd_fseeko(stream, 0, 7) == -1, "unknown whence accepted");
    CHECK(bw_dvd_fclose(stream) == 0, "close failed");
    printf("PASS %s\n", name);
    return 0;
}

static int case_chunk_bound(const char* name) {
    const unsigned long long size = 5ull * 1024ull * 1024ull + 123ull;
    reset(size);
    FILE* stream = bw_dvd_fopen("fake.iso", "rb");
    CHECK(stream != NULL, "open failed");
    unsigned char* buffer = (unsigned char*)malloc((size_t)size);
    CHECK(buffer != NULL, "buffer allocation failed");
    CHECK(bw_dvd_fread(buffer, 1u, (size_t)size, stream) == (size_t)size, "large read short");
    CHECK(bw_fake_image.max_request <= BW_DVD_READ_CHUNK_BYTES, "request exceeded the chunk cap");
    CHECK(bw_fake_image.max_request == BW_DVD_READ_CHUNK_BYTES, "chunk cap was not exercised");
    CHECK(memcmp(buffer, bw_fake_image.bytes, 1024u) == 0, "head bytes differ");
    CHECK(memcmp(buffer + size - 1024u, bw_fake_image.bytes + size - 1024u, 1024u) == 0,
          "tail bytes differ");
    BwDvdIoStats stats;
    bw_dvd_io_stats(&stats);
    CHECK(stats.total_read_bytes == size, "stats total differs");
    CHECK(stats.max_chunk_bytes <= BW_DVD_READ_CHUNK_BYTES, "stats chunk exceeded the cap");
    CHECK(stats.staging_peak_bytes == 0u, "staging bytes recorded on the direct path");
    free(buffer);
    CHECK(bw_dvd_fclose(stream) == 0, "close failed");
    printf("PASS %s\n", name);
    return 0;
}

static int case_open_failure(const char* name) {
    reset(0u);
    bw_fake_image.open_fails = true;
    CHECK(bw_dvd_fopen("fake.iso", "rb") == NULL, "failed open returned a stream");
    printf("PASS %s\n", name);
    return 0;
}

static int case_readahead_bounds(const char* name) {
    const unsigned long long size = 1024ull * 1024ull;
    reset(size);
    CHECK(bw_dvd_io_set_read_ahead(BW_DVD_READ_AHEAD_MAX_BYTES + 4096u) ==
              BW_DVD_READ_AHEAD_MAX_BYTES,
          "read-ahead was not clamped to the cap");
    FILE* stream = bw_dvd_fopen("fake.iso", "rb");
    CHECK(stream != NULL, "open failed");
    unsigned char* buffer = (unsigned char*)malloc((size_t)size);
    CHECK(buffer != NULL, "buffer allocation failed");
    CHECK(bw_dvd_fread(buffer, 1u, (size_t)size, stream) == (size_t)size, "large read short");
    CHECK(memcmp(buffer, bw_fake_image.bytes, (size_t)size) == 0, "bytes differ");
    CHECK(bw_fake_image.max_request <= BW_DVD_READ_AHEAD_MAX_BYTES,
          "read-ahead request exceeded the cap");
    CHECK(bw_fake_image.max_request == BW_DVD_READ_AHEAD_MAX_BYTES,
          "read-ahead window was not exercised");
    BwDvdIoStats stats;
    bw_dvd_io_stats(&stats);
    CHECK(stats.total_read_bytes == size, "stats total differs");
    CHECK(stats.max_chunk_bytes <= BW_DVD_READ_AHEAD_MAX_BYTES, "stats chunk exceeded the cap");
    CHECK(stats.staging_peak_bytes == BW_DVD_READ_AHEAD_MAX_BYTES, "staging bytes differ");
    free(buffer);
    CHECK(bw_dvd_fclose(stream) == 0, "close failed");
    CHECK(bw_dvd_io_set_read_ahead(BW_DVD_READ_AHEAD_MAX_BYTES) == BW_DVD_READ_AHEAD_MAX_BYTES,
          "read-ahead at the cap was rejected");
    CHECK(bw_dvd_io_set_read_ahead(0u) == 0u, "read-ahead disable failed");
    printf("PASS %s\n", name);
    return 0;
}

static int case_readahead_short_read_eof(const char* name) {
    const unsigned long long size = 300ull * 1024ull;
    reset(size);
    bw_dvd_io_set_read_ahead(BW_DVD_READ_AHEAD_MAX_BYTES);
    FILE* stream = bw_dvd_fopen("fake.iso", "rb");
    CHECK(stream != NULL, "open failed");
    CHECK(bw_dvd_fseeko(stream, 100 * 1024, SEEK_SET) == 0, "seek failed");
    unsigned char* buffer = (unsigned char*)malloc((size_t)size);
    CHECK(buffer != NULL, "buffer allocation failed");
    const size_t expected = (size_t)(size - 100ull * 1024ull);
    CHECK(bw_dvd_fread(buffer, 1u, expected + 64u, stream) == expected,
          "read-ahead did not stop at EOF");
    CHECK(memcmp(buffer, bw_fake_image.bytes + 100u * 1024u, expected) == 0,
          "bytes before EOF differ");
    CHECK(bw_dvd_fread(buffer, 1u, 16u, stream) == 0u, "read past EOF returned data");
    free(buffer);
    CHECK(bw_dvd_fclose(stream) == 0, "close failed");
    printf("PASS %s\n", name);
    return 0;
}

static int case_readahead_short_read(const char* name) {
    const unsigned long long size = 256ull * 1024ull;
    reset(size);
    bw_fake_image.max_serve = 4096u;
    bw_dvd_io_set_read_ahead(BW_DVD_READ_AHEAD_MAX_BYTES);
    FILE* stream = bw_dvd_fopen("fake.iso", "rb");
    CHECK(stream != NULL, "open failed");
    const size_t want = 64u * 1024u;
    unsigned char* buffer = (unsigned char*)malloc(want);
    CHECK(buffer != NULL, "buffer allocation failed");
    CHECK(bw_dvd_fread(buffer, 1u, want, stream) == want, "short backend reads lost data");
    CHECK(memcmp(buffer, bw_fake_image.bytes, want) == 0, "short-read bytes differ");
    CHECK(bw_fake_image.max_request <= BW_DVD_READ_AHEAD_MAX_BYTES,
          "short-read request exceeded the cap");
    free(buffer);
    CHECK(bw_dvd_fclose(stream) == 0, "close failed");
    printf("PASS %s\n", name);
    return 0;
}

static int case_readahead_seek(const char* name) {
    const unsigned long long size = 300ull * 1024ull;
    reset(size);
    bw_dvd_io_set_read_ahead(BW_DVD_READ_AHEAD_MAX_BYTES);
    FILE* stream = bw_dvd_fopen("fake.iso", "rb");
    CHECK(stream != NULL, "open failed");
    unsigned char data[100];
    CHECK(bw_dvd_fread(data, 1u, sizeof data, stream) == sizeof data, "first read short");
    CHECK(memcmp(data, bw_fake_image.bytes, sizeof data) == 0, "first read bytes differ");
    CHECK(bw_dvd_fseeko(stream, 200 * 1024, SEEK_SET) == 0, "forward seek failed");
    CHECK(bw_dvd_fread(data, 1u, sizeof data, stream) == sizeof data, "read after seek short");
    CHECK(memcmp(data, bw_fake_image.bytes + 200u * 1024u, sizeof data) == 0,
          "forward seek served stale window bytes");
    CHECK(bw_dvd_fseeko(stream, 50, SEEK_SET) == 0, "backward seek failed");
    CHECK(bw_dvd_fread(data, 1u, sizeof data, stream) == sizeof data, "read after backward seek short");
    CHECK(memcmp(data, bw_fake_image.bytes + 50u, sizeof data) == 0,
          "backward seek served stale window bytes");
    CHECK(bw_dvd_fclose(stream) == 0, "close failed");
    printf("PASS %s\n", name);
    return 0;
}

static int case_readahead_over_report(const char* name) {
    reset(4096u);
    bw_fake_image.over_report = true;
    bw_dvd_io_set_read_ahead(BW_DVD_READ_AHEAD_MAX_BYTES);
    FILE* stream = bw_dvd_fopen("fake.iso", "rb");
    CHECK(stream != NULL, "open failed");
    unsigned char buffer[64];
    CHECK(bw_dvd_fread(buffer, 1u, sizeof buffer, stream) == 0u,
          "over-reported bytes reached the caller");
    CHECK(bw_dvd_fclose(stream) == 0, "close failed");
    printf("PASS %s\n", name);
    return 0;
}

static int case_readahead_two_streams(const char* name) {
    reset(4096u);
    bw_dvd_io_set_read_ahead(BW_DVD_READ_AHEAD_MAX_BYTES);
    FILE* first = bw_dvd_fopen("fake.iso", "rb");
    FILE* second = bw_dvd_fopen("fake.iso", "rb");
    CHECK(first != NULL && second != NULL, "open failed");
    BwDvdIoStats stats;
    bw_dvd_io_stats(&stats);
    CHECK(stats.staging_peak_bytes == 2ull * BW_DVD_READ_AHEAD_MAX_BYTES,
          "staging peak did not sum two open windows");
    CHECK(bw_dvd_fclose(first) == 0, "first close failed");
    CHECK(bw_dvd_fclose(second) == 0, "second close failed");
    printf("PASS %s\n", name);
    return 0;
}

static int case_readahead_default_off(const char* name) {
    const unsigned long long size = 64ull * 1024ull;
    reset(size);
    FILE* stream = bw_dvd_fopen("fake.iso", "rb");
    CHECK(stream != NULL, "open failed");
    unsigned char buffer[4096];
    CHECK(bw_dvd_fread(buffer, 1u, sizeof buffer, stream) == sizeof buffer, "read short");
    CHECK(bw_fake_image.max_request == sizeof buffer, "default path prefetched bytes");
    BwDvdIoStats stats;
    bw_dvd_io_stats(&stats);
    CHECK(stats.staging_peak_bytes == 0u, "default path staged bytes");
    CHECK(bw_dvd_fclose(stream) == 0, "close failed");
    printf("PASS %s\n", name);
    return 0;
}

typedef struct {
    const char* name;
    int (*run)(const char* name);
} Case;

static const Case CASES[] = {
    {"open-close", case_open_close},
    {"read-sequential", case_read_sequential},
    {"short-read-eof", case_short_read_eof},
    {"read-past-eof", case_read_past_eof},
    {"read-failure", case_read_failure},
    {"seek-set-cur-end", case_seek_set_cur_end},
    {"seek-invalid", case_seek_invalid},
    {"chunk-bound", case_chunk_bound},
    {"open-failure", case_open_failure},
    {"readahead-bounds", case_readahead_bounds},
    {"readahead-short-read-eof", case_readahead_short_read_eof},
    {"readahead-short-read", case_readahead_short_read},
    {"readahead-seek", case_readahead_seek},
    {"readahead-over-report", case_readahead_over_report},
    {"readahead-two-streams", case_readahead_two_streams},
    {"readahead-default-off", case_readahead_default_off},
};

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: dvd_io_test <case|all>\n");
        for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; ++i)
            fprintf(stderr, "  %s\n", CASES[i].name);
        return 2;
    }
    int failures = 0;
    for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; ++i) {
        if (strcmp(argv[1], "all") != 0 && strcmp(argv[1], CASES[i].name) != 0)
            continue;
        failures += CASES[i].run(CASES[i].name);
    }
    if (strcmp(argv[1], "all") != 0) {
        bool known = false;
        for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; ++i)
            known = known || strcmp(argv[1], CASES[i].name) == 0;
        if (!known) {
            fprintf(stderr, "unknown case: %s\n", argv[1]);
            return 2;
        }
    }
    return failures == 0 ? 0 : 1;
}
