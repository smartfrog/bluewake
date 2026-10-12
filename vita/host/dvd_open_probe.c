#include "probe_support.h"
#include "gxruntime/dvd.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

static bool fail_trace;

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (fail_trace && stream == stderr && (strncmp(format, "[dvd-open]", 10u) == 0 ||
                                          strncmp(format, "[rel-frontier]", 14u) == 0)) {
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
    const StaticRecompRange ranges[] = {{0x8030F2B0u, 0x8030FC00u}, {0x80240744u, 0x80240748u}};
    const StaticRecompModuleDesc module = {.code_ranges = ranges, .num_code_ranges = 2u};
    host->module = &module;
    CPUState* cpu = &host->cpu;
    const char* scenario = argv[1];
    const bool rel = strncmp(scenario, "rel-", 4u) == 0;
    cpu->pc = strcmp(scenario, "fast") == 0 ? 0x8030F5A4u :
              strcmp(scenario, "entry") == 0 ? 0x8030F2B0u : 0x8030F618u;
    cpu->lr = 0x80001004u;
    cpu->gpr[3] = strcmp(scenario, "fast") == 0 ? 1u : 0x80001000u;
    cpu->gpr[4] = 0x80002000u;
    const char* path = strcmp(scenario, "not-found") == 0 ? "missing.bin" : "boot.bin";
    memcpy(cpu->ram + 0x1000u, path, strlen(path) + 1u);
    if (rel) {
        cpu->pc = 0x80240744u;
        mem_write32(cpu, 0x8000101Cu, 0x80003000u);
        memcpy(cpu->ram + 0x3000u, "synthetic_module", sizeof("synthetic_module"));
        if (strcmp(scenario, "rel-bad-object") == 0)
            cpu->gpr[3] = 0x817FFFF0u;
        else if (strcmp(scenario, "rel-bad-name") == 0)
            mem_write32(cpu, 0x8000101Cu, 0x90000000u);
        else if (strcmp(scenario, "rel-unterminated") == 0)
            memset(cpu->ram + 0x3000u, 'a', 256u);
        else if (strcmp(scenario, "rel-trace") == 0)
            fail_trace = true;
    }
    u8 object_before[0x20u];
    memcpy(object_before, cpu->ram + 0x1000u, sizeof(object_before));
    memset(cpu->ram + 0x1FF0u, 0xA5u, 0x50u);
    if (strcmp(scenario, "bad-path") == 0)
        cpu->gpr[3] = 0x90000000u;
    else if (strcmp(scenario, "unterminated") == 0)
        memset(cpu->ram + 0x1000u, 'a', 256u);
    else if (strcmp(scenario, "bad-file-info") == 0)
        cpu->gpr[4] = 0x817FFFF0u;
    else if (strcmp(scenario, "alias") == 0)
        assert(ppc_guest_alias_add(0x80002008u, 4u, NULL));
    else if (strcmp(scenario, "unowned") == 0)
        host->module = NULL;
    else if (strcmp(scenario, "bad-return") == 0)
        cpu->lr |= 2u;
    else if (strcmp(scenario, "trace") == 0)
        fail_trace = true;
    const bool borrowed = strcmp(scenario, "borrowed") == 0;
    if (borrowed)
        assert(dvd_open_image("ux0:data/bluewake/GZLE01.iso"));
    const u32 address = cpu->pc;
    const bool fails = strcmp(scenario, "missing-disc") == 0 || strcmp(scenario, "bad-path") == 0 ||
                       strcmp(scenario, "unterminated") == 0 || strcmp(scenario, "bad-file-info") == 0 ||
                       strcmp(scenario, "alias") == 0 || strcmp(scenario, "unowned") == 0 ||
                       strcmp(scenario, "trace") == 0 || strcmp(scenario, "bad-return") == 0 || borrowed ||
                       rel;
    assert(ww_host_edge(host, cpu, cpu->pc));
    if (fails) {
        assert(host->error != NULL && host->error_pc == address && host->edge_stopped);
        assert(cpu->pc == address);
        if (rel) {
            assert(memcmp(object_before, cpu->ram + 0x1000u, sizeof(object_before)) == 0);
            if (strcmp(scenario, "rel-frontier") != 0)
                assert(!host->disc_owned && !dvd_image_ready());
            if (strcmp(scenario, "rel-trace") == 0)
                assert(strcmp(host->error, "REL frontier trace write failed") == 0);
        }
        for (u32 index = 0u; index < 0x50u; ++index)
            assert(cpu->ram[0x1FF0u + index] == 0xA5u);
    } else {
        assert(host->error == NULL && !host->edge_stopped && cpu->pc == cpu->lr);
        assert(cpu->gpr[3] == (strcmp(scenario, "not-found") == 0 ? 0u : 1u));
        if (strcmp(scenario, "not-found") != 0 && strcmp(scenario, "entry") != 0) {
            assert(mem_read32(cpu, 0x8000200Cu) == 0u);
            assert(mem_read32(cpu, 0x80002030u) == 0x500u);
            assert(mem_read32(cpu, 0x80002034u) == 0x30u);
            assert(mem_read32(cpu, 0x80002038u) == 0u);
            assert(mem_read32(cpu, 0x80002000u) == 0xA5A5A5A5u);
        }
    }
    assert(host->cycles.absolute_cycles == 0u && host->gx.fifo.size == 0u);
    ww_host_shutdown(host);
    if (borrowed) {
        assert(dvd_image_ready());
        dvd_close_image();
    }
    assert(!dvd_image_ready());
    free(host);
    return 0;
}
