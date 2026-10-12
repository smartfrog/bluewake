#include "probe_support.h"
#include "dvd_file_info.h"
#include "callback_delivery.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

static const char* scenario;
static u32 calls;
static bool fail_trace;

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (fail_trace && stream == stderr && strncmp(format, "[dvd-completion]", 16u) == 0) {
        errno = ENOSPC;
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

static int callback_dispatch(CPUState* cpu, u32 address) {
    assert(address == 0x80003000u || address == 0x80003004u);
    assert((cpu->msr & PPC_MSR_EE) == 0u);
    assert(mem_read32(cpu, 0x803F7A38u) == 4u);
    if (strcmp(scenario, "incomplete") == 0)
        return 0;
    assert(cpu->gpr[3] == 32u && cpu->gpr[4] == 0x80001000u);
    cpu->downcount -= 6;
    ++calls;
    if (address == 0x80003000u) {
        mem_write32(cpu, 0x80004000u, cpu->gpr[3]);
        cpu->gpr[9] = 0xDEADBEEFu;
        cpu->pc = 0x80003004u;
    } else {
        cpu->pc = cpu->lr;
        WwHost* host = cpu->external_user_data;
        assert(cpu->pc == BLUEWAKE_CALLBACK_RETURN_SENTINEL);
        assert(ww_host_edge(host, cpu, cpu->pc));
        assert(!host->edge_stopped && host->error == NULL);
    }
    return 1;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    scenario = argv[1];
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    const StaticRecompRange ranges[] = {{0x8030FADCu, 0x8030FAE0u}, {0x80003000u, 0x80003008u}};
    const StaticRecompModuleDesc module = {.code_ranges = ranges, .num_code_ranges = 2u, .dispatch = callback_dispatch};
    host->module = &module;
    CPUState* cpu = &host->cpu;
    const u32 fi = 0x80001000u;
    mem_write32(cpu, fi + DVD_FI_STARTADDR, 0x500u);
    mem_write32(cpu, fi + DVD_FI_LENGTH, 0x30u);
    mem_write32(cpu, 0x803F7A38u, 3u);
    cpu->pc = 0x8030FADCu;
    cpu->lr = 0x80002004u;
    cpu->gpr[1] = 0x80006000u;
    cpu->gpr[3] = fi;
    cpu->gpr[4] = 0x80005000u;
    cpu->gpr[5] = 32u;
    cpu->gpr[6] = 0u;
    cpu->gpr[7] = 0x80003000u;
    cpu->msr = PPC_MSR_EE;
    if (strcmp(scenario, "full") == 0)
        host->dvd_completions.count = BLUEWAKE_DVD_COMPLETION_CAPACITY;
    else if (strcmp(scenario, "overflow") == 0)
        host->cycles.absolute_cycles = UINT64_MAX - WW_DVD_CALLBACK_DELAY_CYCLES + 1u;
    assert(ww_host_edge(host, cpu, cpu->pc));
    if (strcmp(scenario, "full") == 0 || strcmp(scenario, "overflow") == 0) {
        assert(host->error != NULL && cpu->pc == 0x8030FADCu);
        assert(mem_read32(cpu, 0x80005000u) == 0u && mem_read32(cpu, fi + DVD_CB_STATE) == 0u);
        ww_host_shutdown(host);
        free(host);
        return 0;
    }
    assert(host->error == NULL && cpu->pc == cpu->lr && cpu->gpr[3] == 1u);
    assert(host->dvd_completions.count == 1u && calls == 0u);
    assert(mem_read32(cpu, fi + DVD_CB_STATE) == 1u);
    const u64 ready = host->dvd_completions.entries[0].ready_cycle;
    charge_cycles(host, ready - 1u);
    ww_host_dvd_complete(host);
    assert(calls == 0u && host->dvd_completions.count == 1u);
    charge_cycles(host, 1u);
    if (strcmp(scenario, "masked") == 0) {
        cpu->msr &= ~PPC_MSR_EE;
        ww_host_dvd_complete(host);
        assert(calls == 0u && host->dvd_completions.count == 1u);
        cpu->msr |= PPC_MSR_EE;
    } else if (strcmp(scenario, "scheduler") == 0) {
        cpu->pc = 0x80303A50u;
        ww_host_dvd_complete(host);
        assert(calls == 0u && host->dvd_completions.count == 1u);
        cpu->pc = cpu->lr;
    } else if (strcmp(scenario, "stack") == 0) {
        cpu->gpr[1] = 0u;
    } else if (strcmp(scenario, "trace") == 0) {
        fail_trace = true;
    }
    const CPUState before = *cpu;
    ww_host_dvd_complete(host);
    if (strcmp(scenario, "stack") == 0 || strcmp(scenario, "trace") == 0) {
        assert(host->error != NULL && calls == 0u && host->dvd_completions.count == 1u);
        assert(mem_read32(cpu, fi + DVD_CB_STATE) == 1u && mem_read32(cpu, 0x80004000u) == 0u);
    } else if (strcmp(scenario, "incomplete") == 0) {
        assert(host->error != NULL && host->error_address == 0x80003000u && calls == 0u);
    } else {
        assert(host->error == NULL && calls == 2u && host->dvd_completions.count == 0u);
        assert(mem_read32(cpu, fi + DVD_CB_STATE) == 0u);
        assert(mem_read32(cpu, fi + DVD_CB_XFERRED) == 32u && mem_read32(cpu, 0x80004000u) == 32u);
        assert(memcmp(before.gpr, cpu->gpr, sizeof(before.gpr)) == 0);
        assert(cpu->pc == before.pc && cpu->lr == before.lr && cpu->msr == before.msr);
        assert(mem_read32(cpu, 0x803F7A38u) == 3u);
        assert(host->cycles.absolute_cycles == ready + 12u && cpu->timebase == (ready + 12u) / 12u);
        ww_host_dvd_complete(host);
        assert(calls == 2u);
    }
    assert(!host->dvd_callback_active);
    ww_host_shutdown(host);
    free(host);
    return 0;
}
