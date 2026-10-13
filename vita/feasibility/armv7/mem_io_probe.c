#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#ifndef DIAGNOSTIC_PREFIX
#define DIAGNOSTIC_PREFIX "mem"
#endif
#ifndef DIAGNOSTIC_IO_ENV
#define DIAGNOSTIC_IO_ENV "MEM_IO_FAILURE"
#endif

int __real_fflush(FILE* stream);
int __real_fclose(FILE* stream);
static FILE* report;
static int report_fd = -1;

static int fails(const char* operation) {
    const char* scenario = getenv(DIAGNOSTIC_IO_ENV);
    return scenario && strcmp(scenario, operation) == 0;
}

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (stream == report && fails("write"))
        return -1;
    va_list args;
    va_start(args, format);
    int result = vfprintf(stream, format, args);
    va_end(args);
    return result;
}

int __wrap_fflush(FILE* stream) {
    int result = __real_fflush(stream);
    return stream == report && fails("flush") ? -1 : result;
}

int __wrap_fclose(FILE* stream) {
    int is_report = stream == report;
    int failed = is_report && fails("close");
    int result = __real_fclose(stream);
    if (is_report)
        report = NULL;
    return failed ? -1 : result;
}

int __real_open(const char* path, int flags, ...);
int __wrap_open(const char* path, int flags, ...) {
    va_list arguments;
    va_start(arguments, flags);
    const int mode = flags & O_CREAT ? va_arg(arguments, int) : 0;
    va_end(arguments);
    if ((strcmp(path, DIAGNOSTIC_PREFIX ".partial") == 0 && fails("open")) ||
        (strstr(path, ".v2.") != NULL && fails("rename"))) {
        errno = EIO;
        return -1;
    }
    const int descriptor = __real_open(path, flags, mode);
    if (strcmp(path, DIAGNOSTIC_PREFIX ".partial") == 0)
        report_fd = descriptor;
    return descriptor;
}

FILE* __real_fdopen(int descriptor, const char* mode);
FILE* __wrap_fdopen(int descriptor, const char* mode) {
    FILE* stream = __real_fdopen(descriptor, mode);
    if (descriptor == report_fd)
        report = stream;
    return stream;
}
