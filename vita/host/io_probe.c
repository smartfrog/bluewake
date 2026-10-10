#include "module_exports.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BluewakeEdgeServiceFn edge;
static void* edge_user;

static int dispatch(CPUState* cpu, u32 address) {
    cpu->pc = address;
    cpu->external_write(cpu, 0xCC008000u, 0x61u, 1u);
    cpu->external_write(cpu, 0xCC008000u, 0xFEFFFFFFu, 4u);
    cpu->downcount -= 12;
    if (edge != NULL)
        edge(edge_user, cpu, address + 4u);
    return 1;
}

const StaticRecompModuleDesc* staticrecomp_get_module(void) {
    static const StaticRecompModuleDesc module = {
        .abi_version = STATICRECOMP_ABI_VERSION, .cpu_abi_version = GXRUNTIME_CPU_ABI_VERSION,
        .cpu_state_size = sizeof(CPUState), .game_id = "GZLE01", .entry_point = 0x80001000u,
        .dispatch = dispatch};
    return &module;
}

const BlueWakeRelData* staticrecomp_get_rel_data(u32* count) {
    static const BlueWakeRelData data = {0};
    *count = 0u;
    return &data;
}

void bluewake_set_edge_service(BluewakeEdgeServiceFn callback, void* user) {
    edge = callback;
    edge_user = user;
}

static FILE* report;
static bool failure(const char* name) {
    const char* selected = getenv("WW05_IO_FAILURE");
    return selected != NULL && strcmp(selected, name) == 0;
}

FILE* __real_fopen(const char* path, const char* mode);
FILE* __wrap_fopen(const char* path, const char* mode) {
    FILE* stream = __real_fopen(path, mode);
    if (strcmp(path, "ww05.partial") == 0)
        report = stream;
    return stream;
}

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (stream == report && failure("write")) {
        errno = ENOSPC;
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int __real_fflush(FILE* stream);
int __wrap_fflush(FILE* stream) {
    int result = __real_fflush(stream);
    return stream == report && failure("flush") ? EOF : result;
}

int __real_fclose(FILE* stream);
int __wrap_fclose(FILE* stream) {
    bool fail = stream == report && failure("close");
    int result = __real_fclose(stream);
    return fail ? EOF : result;
}

int __real_rename(const char* from, const char* to);
int __wrap_rename(const char* from, const char* to) {
    if (failure("rename")) {
        errno = EIO;
        return -1;
    }
    return __real_rename(from, to);
}

int __real_fixture_log_close(void);
int __wrap_fixture_log_close(void) {
    return failure("log-close") ? 1 : __real_fixture_log_close();
}
