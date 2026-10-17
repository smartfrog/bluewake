// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BW_DVD_FAKE_BACKEND_H
#define BW_DVD_FAKE_BACKEND_H

// Test-only in-memory backend: one file named "fake.iso", deterministic failure
// injection, request accounting. Shared by the shim tests and the synthetic
// probe so the two never drift.
#include "dvd_io.h"

#include <stdbool.h>

typedef struct {
    unsigned char* bytes;
    unsigned long long size;
    bool open_fails;
    bool read_fails;
    unsigned long long open_calls;
    unsigned long long max_request;
    unsigned long long total_served;
    unsigned long long max_serve;
    bool over_report;
} BwFakeImage;

extern BwFakeImage bw_fake_image;

const BwDvdBackend* bw_fake_backend(void);

// Frees the previous image, zero-allocates `size` bytes, installs the fake
// backend and resets the seam stats. The caller fills bytes as needed.
void bw_fake_install(unsigned long long size);

// Frees the image buffer and clears its bookkeeping.
void bw_fake_release(void);

#endif
