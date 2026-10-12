#include "probe_support.h"
#include "dvd_file_info.h"
#include "gxruntime/dvd.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

static bool fail_trace;

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (fail_trace && stream == stderr && strncmp(format, "[dvd-read]", 10u) == 0) {
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
    const StaticRecompRange ranges[] = {{0x8030FADCu, 0x8030FBD0u}};
    const StaticRecompModuleDesc module = {.code_ranges = ranges, .num_code_ranges = 1u};
    host->module = &module;
    CPUState* cpu = &host->cpu;
    const char* scenario = argv[1];
    const bool asynchronous = strncmp(scenario, "async", 5u) == 0;
    const u32 fi = 0x80001000u;
    memset(cpu->ram + 0x1000u, 0xA5u, 0x40u);
    mem_write32(cpu, fi + DVD_FI_STARTADDR, 0x500u);
    mem_write32(cpu, fi + DVD_FI_LENGTH, 0x30u);
    memset(cpu->ram + 0x1FE0u, 0xA5u, 128u);
    cpu->pc = asynchronous ? 0x8030FADCu : 0x8030FBCCu;
    const u32 address = cpu->pc;
    cpu->lr = 0x80001004u;
    cpu->gpr[3] = fi;
    cpu->gpr[4] = 0x80002000u;
    cpu->gpr[5] = 32u;
    cpu->gpr[6] = strstr(scenario, "padding") != NULL ? 32u : 0u;
    cpu->gpr[7] = asynchronous ? 0u : 2u;
    cpu->gpr[8] = 2u;
    if (strcmp(scenario, "async-callback") == 0)
        cpu->gpr[7] = 0x802BCC78u;
    if (strcmp(scenario, "bad-file-info") == 0)
        cpu->gpr[3] = 0x817FFFF0u;
    else if (strcmp(scenario, "bad-destination") == 0)
        cpu->gpr[4] = 0x817FFFF0u;
    else if (strcmp(scenario, "alias") == 0)
        assert(ppc_guest_alias_add(0x80002010u, 4u, NULL));
    else if (strcmp(scenario, "bad-offset") == 0)
        cpu->gpr[6] = 0x31u;
    else if (strcmp(scenario, "bad-length") == 0)
        cpu->gpr[5] = 0x80u;
    else if (strcmp(scenario, "signed-range") == 0)
        mem_write32(cpu, fi + DVD_FI_STARTADDR, 0x7FFFFFF0u);
    else if (strcmp(scenario, "short-read") == 0)
        mem_write32(cpu, fi + DVD_FI_STARTADDR, 0xFF0u);
    else if (strcmp(scenario, "trace") == 0)
        fail_trace = true;
    const DolInterrupts interrupts = host->interrupts;
    u8 before[0x40];
    memcpy(before, cpu->ram + 0x1000u, sizeof(before));
    assert(ww_host_edge(host, cpu, cpu->pc));
    const bool success = strcmp(scenario, "read") == 0 || strcmp(scenario, "padding") == 0 ||
                         strcmp(scenario, "async") == 0 || strcmp(scenario, "async-padding") == 0;
    if (success) {
        assert(host->error == NULL && !host->edge_stopped && cpu->pc == cpu->lr);
        assert(cpu->gpr[3] == (asynchronous ? 1u : 32u));
        assert(mem_read32(cpu, fi + DVD_CB_STATE) == 0u);
        assert(mem_read32(cpu, fi + DVD_CB_CURRXFER) == 32u);
        assert(mem_read32(cpu, fi + DVD_CB_XFERRED) == 32u);
        assert(mem_read32(cpu, fi + DVD_FI_STARTADDR) == 0x500u);
        assert(mem_read32(cpu, fi + DVD_FI_LENGTH) == 0x30u);
        assert(mem_read32(cpu, fi + DVD_FI_CALLBACK) == 0xA5A5A5A5u);
        for (u32 index = 0u; index < 32u; ++index)
            assert(cpu->ram[0x2000u + index] == (u8)((0x500u + cpu->gpr[6] + index) * 7u + 3u));
    } else {
        assert(host->error != NULL && host->error_pc == address && host->edge_stopped);
        assert(cpu->pc == address && cpu->gpr[3] != 32u);
        if (strcmp(scenario, "async-callback") == 0)
            assert(host->error_address == cpu->gpr[7]);
        assert(memcmp(before, cpu->ram + 0x1000u, sizeof(before)) == 0);
        if (strcmp(scenario, "short-read") != 0)
            for (u32 index = 0u; index < 128u; ++index)
                assert(cpu->ram[0x1FE0u + index] == 0xA5u);
    }
    assert(memcmp(&interrupts, &host->interrupts, sizeof(interrupts)) == 0);
    assert(host->cycles.absolute_cycles == 0u && host->gx.fifo.size == 0u);
    ww_host_shutdown(host);
    assert(!dvd_image_ready());
    free(host);
    return 0;
}
