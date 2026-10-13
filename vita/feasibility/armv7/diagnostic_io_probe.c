#define _POSIX_C_SOURCE 200809L
#include "diagnostic_io.h"
#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>

static const char* scenario;
static FILE* payload;
static int marker = -1;
static bool replaced;
static const char token[] = "cccccccccccccccccccccccccccccccc";

int __real_open(const char* path, int flags, ...);
int __real_close(int descriptor);
int __real_fclose(FILE* stream);
int __real_fflush(FILE* stream);
size_t __real_fwrite(const void* bytes, size_t size, size_t count, FILE* stream);

static void replace_partial(void) {
    assert(!replaced);
    replaced = true;
    assert(rename("capture.partial", "written.partial") == 0);
    if (strstr(scenario, "-symlink") != NULL) {
        assert(symlink("target", "capture.partial") == 0);
    } else {
        FILE* replacement = fopen("capture.partial", "wbx");
        assert(replacement != NULL && fputs("replacement evidence", replacement) >= 0);
        assert(__real_fclose(replacement) == 0);
    }
}

int __wrap_open(const char* path, int flags, ...) {
    va_list arguments;
    va_start(arguments, flags);
    const int mode = flags & O_CREAT ? va_arg(arguments, int) : 0;
    va_end(arguments);
    assert((flags & (O_CREAT | O_EXCL | O_NOFOLLOW)) == (O_CREAT | O_EXCL | O_NOFOLLOW));
    const bool seal = strstr(path, ".v2.") != NULL;
    if (seal && strcmp(scenario, "seal-open-failure") == 0) {
        errno = EIO;
        return -1;
    }
    const int descriptor = __real_open(path, flags, mode);
    if (seal)
        marker = descriptor;
    return descriptor;
}

int __wrap_close(int descriptor) {
    const int result = __real_close(descriptor);
    if (descriptor == marker && !replaced && strncmp(scenario, "partial-after-", 14u) == 0)
        replace_partial();
    if (descriptor == marker && strcmp(scenario, "seal-close-failure") == 0) {
        errno = EIO;
        return -1;
    }
    return result;
}

int __wrap_fflush(FILE* stream) {
    if (stream == payload && !replaced && strncmp(scenario, "partial-before-", 15u) == 0)
        replace_partial();
    if (stream == payload && strcmp(scenario, "flush-failure") == 0) {
        errno = ENOSPC;
        return EOF;
    }
    return __real_fflush(stream);
}

int __wrap_fclose(FILE* stream) {
    const int result = __real_fclose(stream);
    if (stream == payload && strcmp(scenario, "close-failure") == 0) {
        errno = EIO;
        return EOF;
    }
    return result;
}

size_t __wrap_fwrite(const void* bytes, size_t size, size_t count, FILE* stream) {
    if (stream == payload && strcmp(scenario, "write-failure") == 0)
        return __real_fwrite(bytes, size, count - 1u, stream);
    return __real_fwrite(bytes, size, count, stream);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    scenario = argv[1];
    payload = diagnostic_open_candidate("capture.partial");
    if (payload == NULL)
        return 1;
    if (fwrite("new evidence", 1u, 12u, payload) != 12u) {
        const int closed = fclose(payload);
        return closed != 0 ? 2 : 1;
    }
    if (strcmp(scenario, "stop-before-completion") == 0)
        return fclose(payload) != 0 ? 2 : 1;
    return diagnostic_publish(payload, "capture.partial", "capture.final", token) != 0;
}
