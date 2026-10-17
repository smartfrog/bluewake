// SPDX-License-Identifier: GPL-3.0-or-later
// Vita disc I/O seam: the four file primitives the pinned dvd.c is redirected
// to, plus the SceIofilemgr (Vita) and POSIX (host) transports. See dvd_io.h.
#include "dvd_io.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

struct BwDvdStream {
    const BwDvdBackend* backend;
    BwDvdFile* file;
    unsigned long long position;
    unsigned char* window;
    size_t window_size;
    unsigned long long window_offset;
    size_t window_valid;
};

static const BwDvdBackend* g_backend;
static BwDvdIoStats g_stats;
static unsigned g_read_ahead_bytes;
static unsigned long long g_staging_live_bytes;

unsigned bw_dvd_io_set_read_ahead(unsigned bytes) {
    if (bytes > BW_DVD_READ_AHEAD_MAX_BYTES) {
        fprintf(stderr, "[bw-dvd] read-ahead clamped: requested=%u max=%u\n", bytes,
                BW_DVD_READ_AHEAD_MAX_BYTES);
        bytes = BW_DVD_READ_AHEAD_MAX_BYTES;
    }
    g_read_ahead_bytes = bytes;
    return bytes;
}

// ---------------------------------------------------------------------------
// Vita transport (SceIofilemgr, 64-bit offsets)
// ---------------------------------------------------------------------------
#ifdef __vita__

struct BwDvdFile {
    SceUID fd;
};

static BwDvdFile* vita_open(const char* path) {
    const SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return NULL;
    BwDvdFile* file = (BwDvdFile*)malloc(sizeof *file);
    if (file == NULL) {
        sceIoClose(fd);
        return NULL;
    }
    file->fd = fd;
    return file;
}

static long long vita_read_at(BwDvdFile* file, unsigned long long offset, void* dst,
                              unsigned long long length) {
    const int got = sceIoPread(file->fd, dst, (SceSize)length, (SceOff)offset);
    return got < 0 ? -1 : (long long)got;
}

static long long vita_size(BwDvdFile* file) {
    const SceOff end = sceIoLseek(file->fd, 0, SCE_SEEK_END);
    return end < 0 ? -1 : (long long)end;
}

static void vita_close(BwDvdFile* file) {
    sceIoClose(file->fd);
    free(file);
}

static const BwDvdBackend VITA_BACKEND = {"vita-sceio", vita_open, vita_read_at, vita_size,
                                          vita_close};

// ---------------------------------------------------------------------------
// Host transport (POSIX)
// ---------------------------------------------------------------------------
#else

struct BwDvdFile {
    int fd;
};

static BwDvdFile* posix_open(const char* path) {
    const int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    BwDvdFile* file = (BwDvdFile*)malloc(sizeof *file);
    if (file == NULL) {
        close(fd);
        return NULL;
    }
    file->fd = fd;
    return file;
}

static long long posix_read_at(BwDvdFile* file, unsigned long long offset, void* dst,
                               unsigned long long length) {
    const ssize_t got = pread(file->fd, dst, (size_t)length, (off_t)offset);
    return got < 0 ? -1 : (long long)got;
}

static long long posix_size(BwDvdFile* file) {
    struct stat info;
    if (fstat(file->fd, &info) != 0)
        return -1;
    return (long long)info.st_size;
}

static void posix_close(BwDvdFile* file) {
    close(file->fd);
    free(file);
}

static const BwDvdBackend POSIX_BACKEND = {"host-posix", posix_open, posix_read_at, posix_size,
                                           posix_close};

#endif

void bw_dvd_io_set_backend(const BwDvdBackend* backend) {
    g_backend = backend;
}

const BwDvdBackend* bw_dvd_io_backend(void) {
    if (g_backend != NULL)
        return g_backend;
#ifdef __vita__
    return &VITA_BACKEND;
#else
    return &POSIX_BACKEND;
#endif
}

const char* bw_dvd_io_backend_name(void) {
    return bw_dvd_io_backend()->name;
}

void bw_dvd_io_stats(BwDvdIoStats* stats) {
    if (stats != NULL)
        *stats = g_stats;
}

void bw_dvd_io_stats_reset(void) {
    memset(&g_stats, 0, sizeof g_stats);
}

FILE* bw_dvd_fopen(const char* path, const char* mode) {
    if (path == NULL || mode == NULL || mode[0] != 'r' ||
        strspn(mode, "rb") != strlen(mode)) {
        fprintf(stderr, "[bw-dvd] unsupported open mode '%s' for '%s'\n",
                mode != NULL ? mode : "(null)", path != NULL ? path : "(null)");
        return NULL;
    }
    const BwDvdBackend* backend = bw_dvd_io_backend();
    BwDvdFile* file = backend->open(path);
    if (file == NULL) {
        fprintf(stderr, "[bw-dvd] open failed: '%s' backend=%s\n", path, backend->name);
        return NULL;
    }
    struct BwDvdStream* stream = (struct BwDvdStream*)malloc(sizeof *stream);
    if (stream == NULL) {
        backend->close(file);
        fprintf(stderr, "[bw-dvd] stream allocation failed: '%s'\n", path);
        return NULL;
    }
    stream->backend = backend;
    stream->file = file;
    stream->position = 0u;
    stream->window = NULL;
    stream->window_size = 0u;
    stream->window_offset = 0u;
    stream->window_valid = 0u;
    if (g_read_ahead_bytes > 0u) {
        stream->window = (unsigned char*)malloc(g_read_ahead_bytes);
        if (stream->window == NULL) {
            backend->close(file);
            free(stream);
            fprintf(stderr, "[bw-dvd] read-ahead allocation failed: bytes=%u path='%s'\n",
                    g_read_ahead_bytes, path);
            return NULL;
        }
        stream->window_size = g_read_ahead_bytes;
        g_staging_live_bytes += g_read_ahead_bytes;
        if (g_staging_live_bytes > g_stats.staging_peak_bytes)
            g_stats.staging_peak_bytes = g_staging_live_bytes;
    }
    return (FILE*)stream;
}

size_t bw_dvd_fread(void* ptr, size_t size, size_t count, FILE* stream) {
    if (ptr == NULL || stream == NULL || size == 0u || count == 0u)
        return 0u;
    if (count > SIZE_MAX / size) {
        fprintf(stderr, "[bw-dvd] read size overflow: size=%zu count=%zu\n", size, count);
        return 0u;
    }
    struct BwDvdStream* s = (struct BwDvdStream*)stream;
    unsigned long long remaining = (unsigned long long)size * (unsigned long long)count;
    unsigned long long done = 0u;
    while (remaining > 0u) {
        if (s->window != NULL) {
            const unsigned long long in_window =
                s->position >= s->window_offset ? s->position - s->window_offset : 0u;
            if (s->window_valid != 0u && s->position >= s->window_offset &&
                in_window < (unsigned long long)s->window_valid) {
                const size_t available = s->window_valid - (size_t)in_window;
                const unsigned long long take =
                    remaining < (unsigned long long)available ? remaining
                                                             : (unsigned long long)available;
                memcpy((unsigned char*)ptr + done, s->window + (size_t)in_window, (size_t)take);
                done += take;
                s->position += take;
                remaining -= take;
                continue;
            }
            if (s->window_size > g_stats.max_chunk_bytes)
                g_stats.max_chunk_bytes = s->window_size;
            const long long got =
                s->backend->read_at(s->file, s->position, s->window, s->window_size);
            if (got < 0) {
                fprintf(stderr,
                        "[bw-dvd] read-ahead failed: offset=%llu length=%zu backend=%s\n",
                        s->position, s->window_size, s->backend->name);
                break;
            }
            if (got == 0)
                break;
            if ((unsigned long long)got > (unsigned long long)s->window_size) {
                fprintf(stderr,
                        "[bw-dvd] read-ahead over-reported: offset=%llu length=%zu got=%lld "
                        "backend=%s\n",
                        s->position, s->window_size, got, s->backend->name);
                break;
            }
            s->window_offset = s->position;
            s->window_valid = (size_t)got;
            continue;
        }
        const unsigned long long chunk =
            remaining < BW_DVD_READ_CHUNK_BYTES ? remaining : BW_DVD_READ_CHUNK_BYTES;
        if (chunk > g_stats.max_chunk_bytes)
            g_stats.max_chunk_bytes = chunk;
        const long long got = s->backend->read_at(s->file, s->position,
                                                  (unsigned char*)ptr + done, chunk);
        if (got < 0) {
            fprintf(stderr,
                    "[bw-dvd] read failed: offset=%llu length=%llu backend=%s\n",
                    s->position, chunk, s->backend->name);
            break;
        }
        if (got == 0)
            break;
        done += (unsigned long long)got;
        s->position += (unsigned long long)got;
        remaining -= (unsigned long long)got;
    }
    g_stats.total_read_bytes += done;
    return (size_t)(done / size);
}

int bw_dvd_fseeko(FILE* stream, off_t offset, int whence) {
    if (stream == NULL)
        return -1;
    struct BwDvdStream* s = (struct BwDvdStream*)stream;
    long long base;
    switch (whence) {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = (long long)s->position;
        break;
    case SEEK_END: {
        const long long size = s->backend->size(s->file);
        if (size < 0) {
            fprintf(stderr, "[bw-dvd] seek failed: size unavailable backend=%s\n",
                    s->backend->name);
            return -1;
        }
        base = size;
        break;
    }
    default:
        fprintf(stderr, "[bw-dvd] seek failed: unknown whence %d\n", whence);
        return -1;
    }
    const long long next = base + (long long)offset;
    if (next < 0) {
        fprintf(stderr, "[bw-dvd] seek failed: negative position %lld\n", next);
        return -1;
    }
    s->position = (unsigned long long)next;
    s->window_valid = 0u;
    return 0;
}

int bw_dvd_fclose(FILE* stream) {
    if (stream == NULL)
        return -1;
    struct BwDvdStream* s = (struct BwDvdStream*)stream;
    s->backend->close(s->file);
    if (s->window_size > g_staging_live_bytes) {
        fprintf(stderr, "[bw-dvd] staging accounting underflow: window=%zu live=%llu\n",
                s->window_size, g_staging_live_bytes);
        g_staging_live_bytes = 0u;
    } else {
        g_staging_live_bytes -= s->window_size;
    }
    free(s->window);
    free(s);
    return 0;
}
