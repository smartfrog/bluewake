#include "probe_support.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

#ifndef WW_EDGE_OBSERVATION_ADMIT
#define WW_EDGE_OBSERVATION_ADMIT 0
#endif

static unsigned admission_lines;
static bool fail_log;

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    const bool admission = stream == stderr && strncmp(format, "[edge-admit]", 12u) == 0;
    if (admission && fail_log) {
        errno = ENOSPC;
        return -1;
    }
    if (admission)
        ++admission_lines;
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

static void unchanged(const CPUState* cpu, const CPUState* before) {
    assert(cpu->pc == before->pc && cpu->lr == before->lr && cpu->msr == before->msr);
    assert(cpu->cr == before->cr && cpu->xer == before->xer && cpu->ctr == before->ctr);
    assert(cpu->srr0 == before->srr0 && cpu->srr1 == before->srr1);
    assert(cpu->fpscr == before->fpscr);
    assert(memcmp(cpu->gpr, before->gpr, sizeof(cpu->gpr)) == 0);
    assert(memcmp(cpu->fpr, before->fpr, sizeof(cpu->fpr)) == 0);
    assert(memcmp(cpu->gqr, before->gqr, sizeof(cpu->gqr)) == 0);
    assert(mem_read32((CPUState*)cpu, 0x80001000u) == 0xA5B6C7D8u);
}

int main(int argc, char** argv) {
    assert(argc == 3);
    const char* scenario = argv[1];
    const bool legacy = strcmp(scenario, "legacy") == 0;
    const u32 address = (u32)strtoul(argv[2], NULL, 16);
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    const StaticRecompRange range = {0x80000000u, 0x80400000u};
    const StaticRecompModuleDesc module = {.code_ranges = &range, .num_code_ranges = 1u};
    host->module = strcmp(scenario, "unowned") == 0 ? NULL : &module;
    CPUState* cpu = &host->cpu;
    cpu->pc = address + (strcmp(scenario, "pc-mismatch") == 0 ? 4u : 0u);
    cpu->lr = legacy ? address : 0x80002004u;
    cpu->srr0 = 0x80003000u;
    cpu->srr1 = 0x0000A032u;
    for (u32 reg = 0u; reg < 32u; ++reg)
        cpu->gpr[reg] = 0x12340000u | reg;
    mem_write32(cpu, 0x80001000u, 0xA5B6C7D8u);
    if (strcmp(scenario, "alias") == 0)
        assert(ppc_guest_alias_add(address + 3u, 1u, NULL));
    fail_log = strcmp(scenario, "log-failure") == 0;
    const CPUState before = *cpu;
    const bool member = address == 0x800D8DB8u || address == 0x8015D80Cu ||
                        address == 0x8015E3F0u || address == 0x8015EA5Cu ||
                        (legacy && (address == 0x80120188u || address == 0x8022F9FCu));
    const bool admitted = WW_EDGE_OBSERVATION_ADMIT && member &&
        (legacy || strcmp(scenario, "admit") == 0 || strcmp(scenario, "log-once") == 0);
    assert(ww_host_edge(host, cpu, address) == !admitted);
    unchanged(cpu, &before);
    assert(host->cycles.absolute_cycles == 0u && host->gx.fifo.size == 0u);
    assert((host->error == NULL) == admitted && host->edge_stopped == !admitted);
    if (!admitted) {
        const char* expected = WW_EDGE_OBSERVATION_ADMIT && fail_log ? "edge admission log write failed" :
            "desktop interception unsupported; guest not skipped";
        assert(strcmp(host->error, expected) == 0);
        assert(host->error_address == address && host->error_pc == before.pc);
        assert(admission_lines == 0u);
    } else {
        assert(admission_lines == 1u);
        assert(!ww_host_edge(host, cpu, address));
        unchanged(cpu, &before);
        assert(admission_lines == 1u);
        if (strcmp(scenario, "log-once") == 0) {
            cpu->pc = address == 0x8015EA5Cu ? 0x8015E3F0u : 0x8015EA5Cu;
            assert(!ww_host_edge(host, cpu, cpu->pc));
            assert(admission_lines == 2u);
            assert(!ww_host_edge(host, cpu, cpu->pc));
            assert(admission_lines == 2u);
        }
    }
    ww_host_shutdown(host);
    free(host);
    return 0;
}
