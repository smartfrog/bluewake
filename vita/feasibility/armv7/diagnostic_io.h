#ifndef WW_DIAGNOSTIC_IO_H
#define WW_DIAGNOSTIC_IO_H
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static inline bool diagnostic_hex(const char* text, size_t length) {
    if (strlen(text) != length)
        return false;
    for (size_t i = 0; i < length; ++i)
        if (!isxdigit((unsigned char)text[i]))
            return false;
    return true;
}

static inline int diagnostic_request(const char* path, const char* identity,
                              char* token, char* source, char* package) {
    struct stat info;
    if (lstat(path, &info) != 0 || !S_ISREG(info.st_mode))
        return 1;
    FILE* stream = fopen(path, "r");
    if (!stream)
        return 1;
    int fields = fscanf(stream, "%32s %40s %64s", token, source, package);
    char extra;
    int trailing = fscanf(stream, " %c", &extra);
    int error = ferror(stream);
    int closed = fclose(stream);
    return fields != 3 || trailing != EOF || error || closed != 0 ||
           !diagnostic_hex(token, 32) || !diagnostic_hex(source, 40) ||
           !diagnostic_hex(package, 64) || strcmp(source, identity) != 0;
}

static inline bool diagnostic_absent(const char* path) {
    struct stat info;
    return lstat(path, &info) < 0 && errno == ENOENT;
}

static inline int diagnostic_publish_error(const char* stage, const char* partial,
                                          const char* complete, int status, int cause) {
    const int logged = fprintf(stderr, "[diagnostic-publish-error] stage=%s partial=%s complete=%s status=%d errno=%d\n",
                               stage, partial, complete, status, cause);
    errno = cause;
    return logged < 0 ? -1 : status;
}

static inline int diagnostic_publish(FILE* stream, const char* partial, const char* complete) {
    const int flushed = fflush(stream);
    const int flush_errno = errno;
    const int failed = ferror(stream);
    const int closed = fclose(stream);
    if (flushed != 0 || failed || closed != 0) {
        const int cause = flushed != 0 || failed ? flush_errno : errno;
        return diagnostic_publish_error(flushed != 0 || failed ? "flush" : "close", partial, complete,
                                        -1, cause != 0 ? cause : EIO);
    }
    // Path-based publication cannot bind the written inode or safely remove a mutable partial name.
    // No proven identity-bound, no-replace publisher is available under the current output contract.
    // Reports and PNG/MEM1 captures remain unpublished; their partials must not be certified.
    return diagnostic_publish_error("publication-unavailable", partial, complete, -1, ENOSYS);
}
#endif
