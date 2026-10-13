#include "diagnostic_io.h"
#include <assert.h>
#include <unistd.h>

static const char* scenario;
static unsigned publications;

int __real_link(const char* source, const char* destination);
int __real_unlink(const char* path);
int __real_fflush(FILE* stream);
int __real_fclose(FILE* stream);

static void inject_destination(const char* destination) {
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
    return __real_link(source, destination);
}

int __wrap_link(const char* source, const char* destination) {
    return publish_link(source, destination);
}

int __wrap_unlink(const char* path) {
    if (strcmp(scenario, "unlink-failure") == 0) {
        errno = EACCES;
        return -1;
    }
    return __real_unlink(path);
}

int __wrap_fflush(FILE* stream) {
    if (strcmp(scenario, "flush-failure") == 0) {
        errno = ENOSPC;
        return EOF;
    }
    return __real_fflush(stream);
}

int __wrap_fclose(FILE* stream) {
    const int result = __real_fclose(stream);
    if (strcmp(scenario, "flush-failure") == 0 || strcmp(scenario, "close-failure") == 0) {
        errno = EIO;
        return EOF;
    }
    return result;
}

#ifdef __vita__
int sceIoRename(const char* source, const char* destination) {
    if (publish_link(source, destination) != 0)
        return (int)0x80010011u;
    return __real_unlink(source);
}
#endif

int main(int argc, char** argv) {
    assert(argc == 2);
    scenario = argv[1];
    FILE* stream = fopen("capture.partial", "wbx");
    assert(stream != NULL && fputs("new evidence", stream) >= 0);
    const int status = diagnostic_publish(stream, "capture.partial", "capture.final");
    const bool failed = strcmp(scenario, "nominal") != 0;
    assert((status != 0) == failed);
    assert(publications == (strcmp(scenario, "flush-failure") == 0 || strcmp(scenario, "close-failure") == 0 ? 0u : 1u));
    if (strcmp(scenario, "flush-failure") == 0)
        assert(errno == ENOSPC);
    if (strcmp(scenario, "close-failure") == 0)
        assert(errno == EIO);
#ifdef __vita__
    if (strcmp(scenario, "file") == 0 || strcmp(scenario, "symlink") == 0 ||
        strcmp(scenario, "dangling-symlink") == 0 || strcmp(scenario, "publish-failure") == 0)
        assert(status == (int)0x80010011u);
#endif
    return 0;
}
