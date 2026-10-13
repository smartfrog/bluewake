#include "diagnostic_io.h"
#include <assert.h>
#include <unistd.h>

static const char* scenario;
static unsigned publications;
static unsigned unlinks;
static bool partial_replaced;
static bool destination_injected;

int __real_link(const char* source, const char* destination);
int __real_unlink(const char* path);
int __real_fflush(FILE* stream);
int __real_fclose(FILE* stream);

static void replace_partial(const char* source) {
    assert(!partial_replaced);
    partial_replaced = true;
    assert(rename(source, "written.partial") == 0);
    if (strstr(scenario, "-symlink") != NULL) {
        assert(symlink("target", source) == 0);
    } else {
        FILE* replacement = fopen(source, "wbx");
        assert(replacement != NULL);
        assert(fputs("replacement evidence", replacement) >= 0);
        assert(__real_fclose(replacement) == 0);
    }
}

static void inject_destination(const char* destination) {
    if (destination_injected)
        return;
    destination_injected = true;
    if (strcmp(scenario, "file") == 0) {
        FILE* existing = fopen(destination, "wbx");
        assert(existing != NULL);
        assert(fputs("existing evidence", existing) >= 0);
        assert(fclose(existing) == 0);
    } else if (strcmp(scenario, "symlink") == 0 || strcmp(scenario, "dangling-symlink") == 0) {
        assert(symlink(strcmp(scenario, "symlink") == 0 ? "target" : "missing", destination) == 0);
    }
}

static int publish_link(const char* source, const char* destination) {
    ++publications;
    inject_destination(destination);
    if (strcmp(scenario, "publish-failure") == 0) {
        errno = ENOSPC;
        return -1;
    }
    const int result = __real_link(source, destination);
    if (result == 0 && strncmp(scenario, "partial-after-", 14u) == 0)
        replace_partial(source);
    return result;
}

int __wrap_link(const char* source, const char* destination) {
    return publish_link(source, destination);
}

int __wrap_unlink(const char* path) {
    ++unlinks;
    if (strcmp(scenario, "unlink-failure") == 0) {
        errno = EACCES;
        return -1;
    }
    return __real_unlink(path);
}

int __wrap_fflush(FILE* stream) {
    inject_destination("capture.final");
    if (strcmp(scenario, "flush-failure") == 0) {
        errno = ENOSPC;
        return EOF;
    }
    return __real_fflush(stream);
}

int __wrap_fclose(FILE* stream) {
    const int result = __real_fclose(stream);
    if (!partial_replaced && strncmp(scenario, "partial-before-", 15u) == 0)
        replace_partial("capture.partial");
    if (strcmp(scenario, "flush-failure") == 0 || strcmp(scenario, "close-failure") == 0) {
        errno = EIO;
        return EOF;
    }
    return result;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    scenario = argv[1];
    FILE* stream = fopen("capture.partial", "wbx");
    assert(stream != NULL && fputs("new evidence", stream) >= 0);
    struct stat written;
    assert(fstat(fileno(stream), &written) == 0);
    const int status = diagnostic_publish(stream, "capture.partial", "capture.final");
    const int cause = errno;
    if (strncmp(scenario, "partial-", 8u) == 0 || strcmp(scenario, "blocked") == 0) {
        struct stat final;
        if (lstat("capture.final", &final) == 0)
            assert(final.st_dev == written.st_dev && final.st_ino == written.st_ino);
        if (strncmp(scenario, "partial-after-", 14u) == 0 && partial_replaced) {
            struct stat replacement;
            assert(lstat("capture.partial", &replacement) == 0);
        }
        assert(status != 0 && cause == ENOSYS);
        assert(publications == 0u && unlinks == 0u);
        return 0;
    }
    assert(status != 0 && publications == 0u && unlinks == 0u);
    if (strcmp(scenario, "flush-failure") == 0)
        assert(cause == ENOSPC);
    else if (strcmp(scenario, "close-failure") == 0)
        assert(cause == EIO);
    else
        assert(cause == ENOSYS);
    return 0;
}
