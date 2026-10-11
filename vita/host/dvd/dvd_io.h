// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BW_DVD_IO_H
#define BW_DVD_IO_H

// Vita disc I/O seam for the pinned GXRuntime DVD layer.
//
// The pinned GXRuntime/src/dvd.c reads the game image with four stdio
// primitives (fopen/fread/fseeko/fclose). dvd_seam.h redirects those four to
// the shims below when the pin is compiled for this seam; the FST, path
// resolution and guest-RAM copy logic stay the pin's, unmodified.
//
// Transports are backends: the Vita target uses SceIofilemgr (sceIoOpen,
// sceIoPread with a 64-bit offset, sceIoClose), the host uses POSIX
// open/pread/close, and tests install a fake backend.
//
// Reads are positional in the backend and chunked at BW_DVD_READ_CHUNK_BYTES
// directly into the caller's destination: the seam allocates no staging
// buffer, so peak staging memory is zero by construction and bounded by the
// chunk cap per backend call.

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

// One backend read request never exceeds this many bytes. The pinned dvd.c
// asks for whole files; the shim loops over the cap to bound each syscall.
#define BW_DVD_READ_CHUNK_BYTES 0x100000ull

// Reference install path on a Vita (or Vita3K) ux0: the private image lives
// outside the VPK, one file, never committed or published.
#define BW_DVD_INSTALL_PATH "ux0:data/bluewake/GZLE01.iso"

typedef struct BwDvdFile BwDvdFile;

typedef struct {
    const char* name;
    // Returns NULL on failure.
    BwDvdFile* (*open)(const char* path);
    // Reads at an absolute offset; returns bytes read (0 at EOF) or -1.
    long long (*read_at)(BwDvdFile* file, unsigned long long offset, void* dst,
                         unsigned long long length);
    // Returns the file size in bytes or -1.
    long long (*size)(BwDvdFile* file);
    void (*close)(BwDvdFile* file);
} BwDvdBackend;

typedef struct {
    unsigned long long total_read_bytes;
    unsigned long long max_chunk_bytes;
    unsigned long long staging_peak_bytes;
} BwDvdIoStats;

// Install a backend for the process (NULL restores the platform default).
void bw_dvd_io_set_backend(const BwDvdBackend* backend);
const BwDvdBackend* bw_dvd_io_backend(void);
const char* bw_dvd_io_backend_name(void);

void bw_dvd_io_stats(BwDvdIoStats* stats);
void bw_dvd_io_stats_reset(void);

// stdio-shaped shims the pinned dvd.c is redirected to.
FILE* bw_dvd_fopen(const char* path, const char* mode);
size_t bw_dvd_fread(void* ptr, size_t size, size_t count, FILE* stream);
int bw_dvd_fseeko(FILE* stream, off_t offset, int whence);
int bw_dvd_fclose(FILE* stream);

#endif
