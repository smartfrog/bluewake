// SPDX-License-Identifier: GPL-3.0-or-later
// Host contract tests for the disc I/O seam. A fake in-memory backend exercises
// the stdio-shaped shims the pinned dvd.c is redirected to: no real file, no
// pinned runtime, deterministic failure injection.
#include "dvd_io.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char* bytes;
    unsigned long long size;
    bool open_fails;
    bool read_fails;
    unsigned long long open_calls;
    unsigned long long max_request;
    unsigned long long total_served;
} FakeImage;

static FakeImage g_image;

static BwDvdFile* fake_open(const char* path) {
    if (g_image.open_fails || strcmp(path, "fake.iso") != 0)
        return NULL;
    g_image.open_calls++;
    return (BwDvdFile*)&g_image;
}

static long long fake_read_at(BwDvdFile* file, unsigned long long offset, void* dst,
                              unsigned long long length) {
    FakeImage* image = (FakeImage*)file;
    if (image->read_fails)
        return -1;
    if (length > image->max_request)
        image->max_request = length;
    if (offset >= image->size)
        return 0;
    unsigned long long serve = image->size - offset;
    if (serve > length)
        serve = length;
    memcpy(dst, image->bytes + offset, (size_t)serve);
    image->total_served += serve;
    return (long long)serve;
}

static long long fake_size(BwDvdFile* file) {
    return (long long)((FakeImage*)file)->size;
}

static void fake_close(BwDvdFile* file) {
    (void)file;
}

static const BwDvdBackend FAKE_BACKEND = {"fake", fake_open, fake_read_at, fake_size, fake_close};

static void reset(unsigned long long size) {
    free(g_image.bytes);
    memset(&g_image, 0, sizeof g_image);
    if (size != 0u) {
        g_image.bytes = (unsigned char*)malloc((size_t)size);
        if (g_image.bytes == NULL) {
            fprintf(stderr, "test allocation failed\n");
            exit(2);
        }
        for (unsigned long long i = 0; i < size; ++i)
            g_image.bytes[i] = (unsigned char)(i * 131u + 7u);
        g_image.size = size;
    }
    bw_dvd_io_set_backend(&FAKE_BACKEND);
    bw_dvd_io_stats_reset();
}

#define CHECK(condition, message)                     \
    do {                                              \
        if (!(condition)) {                           \
            fprintf(stderr, "FAIL %s: %s\n", name, message); \
            return 1;                                 \
        }                                             \
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
    CHECK(memcmp(first, g_image.bytes, sizeof first) == 0, "first read bytes differ");
    CHECK(bw_dvd_fread(second, 1u, sizeof second, stream) == sizeof second, "second read short");
    CHECK(memcmp(second, g_image.bytes + sizeof first, sizeof second) == 0,
          "second read did not continue the position");
    unsigned char pair[8];
    CHECK(bw_dvd_fread(pair, 2u, 4u, stream) == 4u, "item read count differs");
    CHECK(memcmp(pair, g_image.bytes + 512u, sizeof pair) == 0, "item read bytes differ");
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
    CHECK(memcmp(tail, g_image.bytes + 4090u, 6u) == 0, "tail bytes differ");
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
    g_image.read_fails = true;
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
    CHECK(memcmp(data, g_image.bytes + 100u, sizeof data) == 0, "SEEK_SET bytes differ");
    CHECK(bw_dvd_fseeko(stream, -8, SEEK_CUR) == 0, "SEEK_CUR failed");
    CHECK(bw_dvd_fread(data, 1u, sizeof data, stream) == sizeof data, "read after SEEK_CUR short");
    CHECK(memcmp(data, g_image.bytes + 100u, sizeof data) == 0, "SEEK_CUR bytes differ");
    CHECK(bw_dvd_fseeko(stream, -4, SEEK_END) == 0, "SEEK_END failed");
    CHECK(bw_dvd_fread(data, 1u, sizeof data, stream) == 4u, "SEEK_END tail not short");
    CHECK(memcmp(data, g_image.bytes + 4092u, 4u) == 0, "SEEK_END bytes differ");
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
    CHECK(g_image.max_request <= BW_DVD_READ_CHUNK_BYTES, "request exceeded the chunk cap");
    CHECK(g_image.max_request == BW_DVD_READ_CHUNK_BYTES, "chunk cap was not exercised");
    CHECK(memcmp(buffer, g_image.bytes, 1024u) == 0, "head bytes differ");
    CHECK(memcmp(buffer + size - 1024u, g_image.bytes + size - 1024u, 1024u) == 0,
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
    g_image.open_fails = true;
    CHECK(bw_dvd_fopen("fake.iso", "rb") == NULL, "failed open returned a stream");
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
