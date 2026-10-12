#include "probe_support.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

static bool fail_trace;

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (fail_trace && stream == stderr && strncmp(format, "[scheduler-idle", 15u) == 0) {
        errno = ENOSPC;
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    CPUState* cpu = &host->cpu;
    cpu->pc = 0x80307EF4u;
    cpu->gpr[13] = 0x8040E0E0u;
    const StaticRecompRange range = {cpu->pc, cpu->pc + 4u};
    const StaticRecompModuleDesc module = {.code_ranges = &range, .num_code_ranges = 1u};
    host->module = &module;
    host->rel_image.linked = strcmp(argv[1], "unlinked") != 0;
    host->rel_image.module_id = 1u;
    mem_write16(cpu, 0x803A2960u + 0x2C8u, 4u);
    mem_write32(cpu, 0x803A2960u + 0x2DCu, 0x80006008u);
    mem_write32(cpu, 0x803A2960u + 0x198u, 0x803059D0u);
    mem_write32(cpu, 0x803A2960u + 4u, 0x80007000u);
    mem_write32(cpu, 0x80007004u, 0x80001234u);
    mem_write32(cpu, 0x800000DCu, 0x803A2960u);
    const u32 size = cpu->ram_size;
    u8* before = malloc(size);
    assert(before != NULL);
    memcpy(before, cpu->ram, size);
    fail_trace = strcmp(argv[1], "trace") == 0;
    const bool intercepted = ww_host_edge(host, cpu, cpu->pc);
    if (fail_trace) {
        assert(intercepted && host->error != NULL && host->edge_stopped);
        assert(strstr(host->error, "scheduler") != NULL);
    } else {
        assert(!intercepted && host->error == NULL && cpu->exception == 0u);
        if (strcmp(argv[1], "repeat") == 0)
            assert(!ww_host_edge(host, cpu, cpu->pc));
    }
    assert(cpu->pc == 0x80307EF4u && host->cycles.absolute_cycles == 0u);
    assert(memcmp(before, cpu->ram, size) == 0);
    free(before);
    ww_host_shutdown(host);
    free(host);
    return 0;
}
