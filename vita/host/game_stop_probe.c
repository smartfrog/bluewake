#include "probe_support.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

static const char* failed_trace;

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (stream == stderr && failed_trace != NULL &&
        strncmp(format, failed_trace, strlen(failed_trace)) == 0) {
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
    assert(argc >= 2);
    const char* scenario = argv[1];
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    CPUState* cpu = &host->cpu;
    cpu->msr = PPC_MSR_DR | PPC_MSR_IR;
    cpu->pc = 0x80006C4Cu;
    cpu->lr = 0x80001234u;
    cpu->srr0 = 0x80005678u;
    cpu->gpr[1] = 0x80007000u;
    cpu->gpr[3] = 0x80008000u;
    cpu->gpr[4] = 321u;
    cpu->gpr[5] = 0x80008100u;
    cpu->gpr[6] = 0x12345678u;
    cpu->gpr[7] = 0x87654321u;
    cpu->gpr[8] = 0xAABBCCDDu;
    for (u32 reg = 26u; reg < 32u; ++reg)
        cpu->gpr[reg] = 0xB0000000u | reg;
    memcpy(cpu->ram + 0x8000u, "synthetic_assert.cpp", 21u);
    memcpy(cpu->ram + 0x8100u, "ship assertion %s:%u", 20u);
    mem_write32(cpu, 0x800000E4u, 0x803A2960u);
    mem_write32(cpu, 0x800000D4u, 0x803A2970u);
    mem_write32(cpu, 0x803F7A30u, 0x40000000u);
    for (u32 index = 0u; index < 10u; ++index) {
        mem_write32(cpu, 0x80007000u + index * 16u, 0x80007010u + index * 16u);
        mem_write32(cpu, 0x80007004u + index * 16u, 0x80002000u + index * 4u);
    }
    mem_write32(cpu, 0x80007010u, 0u);
    memset(cpu->ram + 0x01234560u, 0xA5u, 16u);
    if (strcmp(scenario, "text-null") == 0)
        cpu->gpr[3] = cpu->gpr[5] = 0u;
    if (strcmp(scenario, "text-long") == 0) {
        memset(cpu->ram + 0x8000u, 'F', 160u);
        memset(cpu->ram + 0x8100u, 'M', 256u);
    }
    if (strcmp(scenario, "text-invalid") == 0)
        cpu->gpr[5] = 0xCC000008u;
    if (strcmp(scenario, "text-wrap") == 0)
        cpu->gpr[5] = UINT32_MAX;
    if (strcmp(scenario, "text-alias") == 0) {
        static const u8 text[] = "linked assertion";
        assert(ppc_guest_alias_add(0x83000000u, sizeof(text), text));
        cpu->gpr[5] = 0x83000000u;
    }
    if (strcmp(scenario, "panic-trace") == 0)
        failed_trace = "[panic]";
    const bool marker = strncmp(scenario, "marker", 6u) == 0;
    const bool halt = strncmp(scenario, "halt", 4u) == 0;
    const bool walk = strncmp(scenario, "walk", 4u) == 0;
    const bool rejected_text = strcmp(scenario, "text-invalid") == 0 || strcmp(scenario, "text-wrap") == 0;
    u32 registers[32];
    memcpy(registers, cpu->gpr, sizeof(registers));
    if (!halt && !walk && strcmp(scenario, "marker-late") != 0 && strcmp(scenario, "unrelated-edge") != 0) {
        const bool stopped = ww_host_edge(host, cpu, cpu->pc);
        if (failed_trace != NULL || rejected_text) {
            assert(stopped && host->edge_stopped && host->error != NULL);
            assert(strstr(host->error, "OSPanic") != NULL);
        } else {
            assert(!stopped && host->error == NULL && cpu->exception == 0u);
        }
        if (strcmp(scenario, "panic-repeat") == 0) {
            cpu->gpr[3] = cpu->gpr[4] = cpu->gpr[5] = 0u;
            assert(!ww_host_edge(host, cpu, cpu->pc));
            memcpy(cpu->gpr, registers, sizeof(registers));
        }
    }
    if (halt || walk) {
        cpu->pc = strcmp(scenario, "halt-nop") == 0 ? 0x80301510u : 0x8030150Cu;
        if (walk) {
            assert(argc == 3);
            const u32 terminator = (u32)strtoul(argv[2], NULL, 0);
            if (strcmp(scenario, "walk-initial") == 0)
                cpu->gpr[1] = terminator;
            else
                mem_write32(cpu, 0x80007010u, terminator);
        }
        if (strcmp(scenario, "halt-trace") == 0)
            failed_trace = "[halt]";
        if (strcmp(scenario, "halt-frame-trace") == 0)
            failed_trace = "[halt] frame";
        assert(ww_host_edge(host, cpu, cpu->pc));
        assert(host->edge_stopped && host->error != NULL);
        assert(strstr(host->error, failed_trace != NULL ? "trace" : "PPCHalt") != NULL);
        registers[1] = cpu->gpr[1];
    } else if (marker) {
        cpu->gpr[3] = cpu->gpr[4] = cpu->gpr[5] = 0u;
        u32 address = 0x01234567u;
        u64 value = 0x01234567u;
        u8 width = 4u;
        cpu->pc = 0x80006D64u;
        if (strcmp(scenario, "marker-reject") == 0) {
            assert(argc == 6);
            address = (u32)strtoul(argv[2], NULL, 0);
            value = strtoull(argv[3], NULL, 0);
            width = (u8)strtoul(argv[4], NULL, 0);
            cpu->pc = (u32)strtoul(argv[5], NULL, 0);
        }
        if (strcmp(scenario, "marker-frame-trace") == 0)
            failed_trace = "[halt] frame";
        assert(get_ram_ptr(cpu, address, width, NULL) == NULL);
        if (width == 4u)
            mem_write32(cpu, address, (u32)value);
        else
            cpu->external_write(cpu, address, value, width);
        assert(host->error != NULL && cpu->exception != 0u);
        const char* reason = strcmp(scenario, "marker-reject") == 0 ? "unsupported MMIO write" :
                             failed_trace != NULL ? "halt frame trace write failed" : "game panic (OSPanic)";
        assert(strcmp(host->error, reason) == 0);
        assert(host->error_address == address && host->error_pc == cpu->pc);
        assert(host->error_width == width && host->error_value == value);
        for (u32 index = 0u; index < 16u; ++index)
            assert(cpu->ram[0x01234560u + index] == 0xA5u);
        memcpy(registers, cpu->gpr, sizeof(registers));
    } else if (strcmp(scenario, "unrelated-edge") == 0) {
        cpu->pc = 0x80006C50u;
        assert(!ww_host_edge(host, cpu, cpu->pc) && host->error == NULL);
    }
    assert(memcmp(cpu->gpr, registers, sizeof(registers)) == 0);
    assert(host->cycles.absolute_cycles == 0u && cpu->timebase == 0u);
    assert(cpu->lr == 0x80001234u && cpu->srr0 == 0x80005678u);
    ww_host_shutdown(host);
    free(host);
    return 0;
}
