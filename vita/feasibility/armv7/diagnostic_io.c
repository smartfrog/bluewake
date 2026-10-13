#define _POSIX_C_SOURCE 200809L
#include "diagnostic_io.h"
#include "mbedtls/sha256.h"
#include <fcntl.h>
#include <unistd.h>

static int exclusive_fd(const char* path) {
    int flags = O_RDWR | O_CREAT | O_EXCL;
#ifndef __vita__
    flags |= O_NOFOLLOW | O_CLOEXEC;
#endif
    return open(path, flags, 0600);
}

FILE* diagnostic_open_candidate(const char* path) {
    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return NULL;
    }
    const int descriptor = exclusive_fd(path);
    if (descriptor < 0)
        return NULL;
    FILE* stream = fdopen(descriptor, "w+b");
    if (stream == NULL) {
        const int cause = errno;
        const int closed = close(descriptor);
        if (closed != 0)
            diagnostic_publish_error("fdopen-and-close", path, path, -1, cause);
        errno = cause;
    }
    return stream;
}

int diagnostic_publish(FILE* stream, const char* partial, const char* complete, const char* token) {
    int cause = 0;
    const char* stage = "identity";
    unsigned char digest[32];
    unsigned long long size = 0u;
    if (stream == NULL || partial == NULL || complete == NULL || token == NULL)
        return diagnostic_publish_error(stage, partial != NULL ? partial : "(null)",
                                        complete != NULL ? complete : "(null)", -1, EINVAL);
    if (!diagnostic_hex(token, 32u) || strspn(token, "0123456789abcdef") != 32u)
        cause = EINVAL;
    if (cause == 0) {
        stage = "flush";
        if (fflush(stream) != 0 || ferror(stream))
            cause = errno != 0 ? errno : EIO;
    }
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    if (cause == 0) {
        stage = "hash";
        if (fseek(stream, 0, SEEK_END) != 0)
            cause = errno != 0 ? errno : EIO;
        const long extent = cause == 0 ? ftell(stream) : -1;
        if (extent < 0 || fseek(stream, 0, SEEK_SET) != 0)
            cause = errno != 0 ? errno : EIO;
        if (cause == 0 && mbedtls_sha256_starts_ret(&hash, 0) != 0)
            cause = EIO;
        unsigned char bytes[32768];
        while (cause == 0 && size < (unsigned long long)extent) {
            const unsigned long long remaining = (unsigned long long)extent - size;
            const size_t count = remaining < sizeof(bytes) ? (size_t)remaining : sizeof(bytes);
            if (fread(bytes, 1u, count, stream) != count ||
                mbedtls_sha256_update_ret(&hash, bytes, count) != 0) {
                cause = errno != 0 ? errno : EIO;
                break;
            }
            size += count;
        }
        if (cause == 0 && (fgetc(stream) != EOF || ferror(stream) ||
                          mbedtls_sha256_finish_ret(&hash, digest) != 0))
            cause = errno != 0 ? errno : EIO;
    }
    mbedtls_sha256_free(&hash);
    const int closed = fclose(stream);
    if (cause == 0 && closed != 0) {
        stage = "payload-close";
        cause = errno != 0 ? errno : EIO;
    }
    if (cause != 0)
        return diagnostic_publish_error(stage, partial, complete, -1, cause);
    char hex[65], seal[512];
    for (unsigned index = 0u; index < sizeof(digest); ++index)
        snprintf(hex + index * 2u, 3u, "%02x", digest[index]);
    const int length = snprintf(seal, sizeof(seal), "%s.v2.%s.%s.%llu.seal", complete, token, hex, size);
    if (length < 0 || (size_t)length >= sizeof(seal))
        return diagnostic_publish_error("seal-path", partial, complete, -1, ENAMETOOLONG);
    const int marker = exclusive_fd(seal);
    if (marker < 0)
        return diagnostic_publish_error("seal-open", partial, complete, -1, errno);
    if (close(marker) != 0)
        return diagnostic_publish_error("seal-close", partial, complete, -1, errno);
    return 0;
}
