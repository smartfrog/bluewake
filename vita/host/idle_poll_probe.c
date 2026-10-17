#include "probe_support.h"

#include "guest_timing.h"

#include <stdlib.h>

#define IDLE_PC 0x80307EF4u
#define IDLE_POLL 0x803F7A30u
#define IDLE_R13 0x803FE0E0u
#define VI_DI0 (DOL_VI_BASE + DOL_VI_DI0_OFF)

int main(void) {
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    CPUState* cpu = &host->cpu;
    cpu->pc = IDLE_PC;
    cpu->msr = PPC_MSR_EE;
    cpu->gpr[13] = IDLE_R13;
    host->scheduler_idle_observed = true;
    mem_write32(cpu, IDLE_POLL, 0u);
    assert(ww_host_idle_poll_ready(host));

    mem_write32(cpu, IDLE_POLL, 0x8000u);
    assert(!ww_host_idle_poll_ready(host));
    mem_write32(cpu, IDLE_POLL, 0u);

    cpu->pc = 0x80307EF8u;
    assert(!ww_host_idle_poll_ready(host));
    cpu->pc = IDLE_PC;

    cpu->msr = 0u;
    assert(!ww_host_idle_poll_ready(host));
    cpu->msr = PPC_MSR_EE;

    host->dvd_completions.count = 1u;
    assert(!ww_host_idle_poll_ready(host));
    host->dvd_completions.count = 0u;

    cpu->gpr[13] = IDLE_R13 + 4u;
    assert(!ww_host_idle_poll_ready(host));
    cpu->gpr[13] = IDLE_R13;

    host->scheduler_idle_observed = false;
    assert(!ww_host_idle_poll_ready(host));
    host->scheduler_idle_observed = true;

    bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
    bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
    assert(cpu->cycle_budget == 256);

    host->vi_clock_enabled = true;
    cpu->external_write(cpu, VI_DI0, 0x1000u, 2u);
    cpu->external_write(cpu, DOL_PI_INTERRUPT_MASK, DOL_PI_CAUSE_VI, 4u);
#if WW_IDLE_DEADLINE_ADVANCE
    assert(host->cycles.idle_advance_cap == GUEST_CPU_CYCLES_PER_VI_RETRACE);
#else
    assert(host->cycles.idle_advance_cap == 0);
    bluewake_cycle_domain_set_idle_advance(&host->cycles,
                                           GUEST_CPU_CYCLES_PER_VI_RETRACE);
#endif
    bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
    bluewake_cycle_domain_set_idle_turn(&host->cycles,
                                        ww_host_idle_poll_ready(host));
    bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
    assert(cpu->cycle_budget == GUEST_CPU_CYCLES_PER_VI_RETRACE);
    assert(cpu->cycle_deadline_active == 1u);
    cpu->downcount = -(s64)cpu->cycle_budget;
    assert(bluewake_cycle_domain_end_turn(&host->cycles, cpu) ==
           GUEST_CPU_CYCLES_PER_VI_RETRACE);
    assert(host->vi_retraces == 1u);
    assert(dol_interrupts_external_pending(&host->interrupts));

    cpu->pc = 0x00000500u;
    assert(!ww_host_idle_poll_ready(host));
    bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
    bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
    assert(cpu->cycle_budget == 256);
    return 0;
}
