// SPDX-License-Identifier: GPL-3.0-or-later
#include "dvd_fake_backend.h"

#include <stdlib.h>
#include <string.h>

BwFakeImage bw_fake_image;

static BwDvdFile* fake_open(const char* path) {
    if (bw_fake_image.open_fails || path == NULL || strcmp(path, "fake.iso") != 0)
        return NULL;
    bw_fake_image.open_calls++;
    return (BwDvdFile*)&bw_fake_image;
}

static long long fake_read_at(BwDvdFile* file, unsigned long long offset, void* dst,
                              unsigned long long length) {
    BwFakeImage* image = (BwFakeImage*)file;
    if (image->read_fails)
        return -1;
    if (image->over_report)
        return (long long)(length + 1u);
    if (length > image->max_request)
        image->max_request = length;
    if (offset >= image->size)
        return 0;
    unsigned long long serve = image->size - offset;
    if (serve > length)
        serve = length;
    if (image->max_serve != 0u && serve > image->max_serve)
        serve = image->max_serve;
    memcpy(dst, image->bytes + offset, (size_t)serve);
    image->total_served += serve;
    return (long long)serve;
}

static long long fake_size(BwDvdFile* file) {
    return (long long)((BwFakeImage*)file)->size;
}

static void fake_close(BwDvdFile* file) {
    (void)file;
}

static const BwDvdBackend FAKE_BACKEND = {"fake", fake_open, fake_read_at, fake_size, fake_close};

const BwDvdBackend* bw_fake_backend(void) {
    return &FAKE_BACKEND;
}

void bw_fake_install(unsigned long long size) {
    bw_fake_release();
    memset(&bw_fake_image, 0, sizeof bw_fake_image);
    if (size != 0u) {
        bw_fake_image.bytes = (unsigned char*)calloc(1u, (size_t)size);
        if (bw_fake_image.bytes == NULL)
            abort();
        bw_fake_image.size = size;
    }
    bw_dvd_io_set_backend(&FAKE_BACKEND);
    bw_dvd_io_stats_reset();
}

void bw_fake_release(void) {
    free(bw_fake_image.bytes);
    bw_fake_image.bytes = NULL;
    bw_fake_image.size = 0u;
}
