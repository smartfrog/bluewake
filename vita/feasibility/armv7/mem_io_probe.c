#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DIAGNOSTIC_PREFIX
#define DIAGNOSTIC_PREFIX "mem"
#endif
#ifndef DIAGNOSTIC_IO_ENV
#define DIAGNOSTIC_IO_ENV "MEM_IO_FAILURE"
#endif

FILE* __real_fopen(const char* path, const char* mode);
int __real_fflush(FILE* stream);
int __real_fclose(FILE* stream);
int __real_rename(const char* source, const char* destination);
static FILE* report;

static int fails(const char* operation) {
    const char* scenario = getenv(DIAGNOSTIC_IO_ENV);
    return scenario && strcmp(scenario, operation) == 0;
}

FILE* __wrap_fopen(const char* path, const char* mode) {
    if (strcmp(path, DIAGNOSTIC_PREFIX ".partial") == 0 && fails("open"))
        return NULL;
    FILE* stream = __real_fopen(path, mode);
    if (strcmp(path, DIAGNOSTIC_PREFIX ".partial") == 0)
        report = stream;
    return stream;
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

int __wrap_rename(const char* source, const char* destination) {
    return fails("rename") ? -1 : __real_rename(source, destination);
}
