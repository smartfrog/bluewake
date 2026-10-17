#include "probe_support.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

#ifndef WW05_ENABLE_VI_CLOCK
#error "vi_clock_probe requires the configured WW05_ENABLE_VI_CLOCK value; ww05_services must export it"
#endif

#define RETRACE_CYCLES 8100000ull
#define VI_DI0 (DOL_VI_BASE + DOL_VI_DI0_OFF)

static bool fail_trace;

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (fail_trace && stream == stderr && strncmp(format, "[vi-retrace]", 12u) == 0) {
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
    const char* scenario = argv[1];
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    CPUState* cpu = &host->cpu;
    cpu->pc = 0x80001000u;
    const StaticRecompRange range = {cpu->pc, cpu->pc + 4u};
    const StaticRecompModuleDesc module = {.code_ranges = &range, .num_code_ranges = 1u};
    host->module = &module;
#if WW05_ENABLE_VI_CLOCK
    assert(host->vi_clock_enabled);
#else
    assert(!host->vi_clock_enabled);
#endif
    if (strcmp(scenario, "off") == 0)
        host->vi_clock_enabled = false;
    else
        host->vi_clock_enabled = true;
    cpu->external_write(cpu, VI_DI0, 0x1000u, 2u);
    cpu->external_write(cpu, DOL_PI_INTERRUPT_MASK, DOL_PI_CAUSE_VI, 4u);
    u8* before = malloc(cpu->ram_size);
    assert(before != NULL);
    memcpy(before, cpu->ram, cpu->ram_size);
    if (strcmp(scenario, "off") == 0) {
        charge_cycles(host, RETRACE_CYCLES);
        assert(host->vi_retraces == 0u && !dol_interrupts_external_pending(&host->interrupts));
    } else if (strcmp(scenario, "threshold") == 0) {
        charge_cycles(host, RETRACE_CYCLES - 1u);
        assert(host->vi_retraces == 0u && !dol_interrupts_external_pending(&host->interrupts));
        charge_cycles(host, 1u);
        assert(host->vi_retraces == 1u && dol_interrupts_external_pending(&host->interrupts));
        assert(cpu->external_read(cpu, VI_DI0, 2u) == 0x9000u);
        assert(cpu->timebase == 675000u && host->cycles.absolute_cycles == RETRACE_CYCLES);
    } else if (strcmp(scenario, "deadline") == 0) {
        charge_cycles(host, RETRACE_CYCLES - 11u);
        bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
        assert(cpu->cycle_budget == 11 && cpu->cycle_deadline_active);
        cpu->downcount = -11;
        assert(ww_host_end_dispatch(host) == 11u && host->vi_retraces == 1u);
        assert(cpu->timebase == 675000u);
    } else if (strcmp(scenario, "masked") == 0) {
        cpu->external_write(cpu, VI_DI0, 0u, 2u);
        charge_cycles(host, RETRACE_CYCLES);
        assert(host->vi_retraces == 1u && !dol_interrupts_external_pending(&host->interrupts));
        assert(cpu->external_read(cpu, VI_DI0, 2u) == 0x8000u);
        cpu->external_write(cpu, VI_DI0, 0x9000u, 2u);
        assert(dol_interrupts_external_pending(&host->interrupts));
    } else if (strcmp(scenario, "pi-masked") == 0) {
        cpu->external_write(cpu, DOL_PI_INTERRUPT_MASK, 0u, 4u);
        charge_cycles(host, RETRACE_CYCLES);
        assert(host->vi_retraces == 1u && !dol_interrupts_external_pending(&host->interrupts));
        assert(dol_interrupts_pi_cause(&host->interrupts) & DOL_PI_CAUSE_VI);
        cpu->external_write(cpu, DOL_PI_INTERRUPT_MASK, DOL_PI_CAUSE_VI, 4u);
        assert(dol_interrupts_external_pending(&host->interrupts));
    } else if (strcmp(scenario, "ee") == 0 || strcmp(scenario, "exception") == 0) {
        charge_cycles(host, RETRACE_CYCLES);
        if (strcmp(scenario, "exception") == 0) {
            cpu->msr = PPC_MSR_EE;
            cpu->exception = PPC_EXC_PROGRAM;
        }
        assert(!ww_host_edge(host, cpu, cpu->pc));
        assert(cpu->pc == 0x80001000u && dol_interrupts_external_pending(&host->interrupts));
        cpu->exception = 0u;
        cpu->msr = PPC_MSR_EE | PPC_MSR_IR | PPC_MSR_DR;
        assert(ww_host_edge(host, cpu, cpu->pc));
        assert(cpu->pc == 0x500u && cpu->srr0 == 0x80001000u && cpu->exception == 0u);
        assert(cpu->srr1 == (PPC_MSR_EE | PPC_MSR_IR | PPC_MSR_DR));
        assert(!(cpu->msr & PPC_MSR_EE));
    } else if (strcmp(scenario, "ack") == 0) {
        charge_cycles(host, RETRACE_CYCLES);
        assert(host->vi_retraces == 1u && dol_interrupts_external_pending(&host->interrupts));
        cpu->external_write(cpu, VI_DI0, 0x1000u, 2u);
        assert(!dol_interrupts_external_pending(&host->interrupts));
        charge_cycles(host, RETRACE_CYCLES);
        assert(host->vi_retraces == 2u && dol_interrupts_external_pending(&host->interrupts));
    } else if (strcmp(scenario, "multiple") == 0) {
        charge_cycles(host, RETRACE_CYCLES * 3u + 7u);
        assert(host->vi_retraces == 3u && cpu->timebase == 2025000u);
        assert(dol_vi_clock_now(&host->vi_clock) == RETRACE_CYCLES * 3u + 7u);
        assert(host->clock_remainder == 7u);
    } else if (strcmp(scenario, "zero") == 0) {
        for (u32 index = 0u; index < 64u; ++index) {
            charge_cycles(host, 0u);
            assert(!ww_host_edge(host, cpu, cpu->pc));
        }
        assert(host->vi_retraces == 0u && cpu->timebase == 0u && host->cycles.absolute_cycles == 0u);
        assert(!dol_interrupts_external_pending(&host->interrupts));
    } else if (strcmp(scenario, "timebase-write") == 0) {
        charge_cycles(host, RETRACE_CYCLES - 1u);
        ppc_mtspr(cpu, 284u, 0xFFFFFFFFu, cpu->pc);
        ppc_mtspr(cpu, 285u, 0x7FFFFFFFu, cpu->pc);
        assert(host->vi_retraces == 0u && !dol_interrupts_external_pending(&host->interrupts));
        charge_cycles(host, 1u);
        assert(host->vi_retraces == 1u && dol_interrupts_external_pending(&host->interrupts));
    } else if (strcmp(scenario, "trace") == 0) {
        fail_trace = true;
        charge_cycles(host, RETRACE_CYCLES);
        assert(host->error != NULL && strstr(host->error, "VI") != NULL);
        assert(ww_host_edge(host, cpu, cpu->pc) && host->edge_stopped);
    } else {
        assert(!"unknown VI clock scenario");
    }
    if (!fail_trace)
        assert(host->error == NULL);
    assert(host->stream.decoded == 0u && host->frontier.size == 0u && host->renderer == NULL);
    assert(host->dvd_completions.count == 0u && memcmp(before, cpu->ram, cpu->ram_size) == 0);
    free(before);
    ww_host_shutdown(host);
    free(host);
    return 0;
}
