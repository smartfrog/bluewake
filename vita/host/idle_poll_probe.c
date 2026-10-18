#include "probe_support.h"

#include "guest_timing.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char jump_line[160];
static char taken_line[160];
static unsigned jump_hits;
static unsigned taken_hits;

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    if (stream == stderr && strncmp(format, "[idle-jump]", 11u) == 0) {
        va_list copy;
        va_copy(copy, arguments);
        vsnprintf(jump_line, sizeof(jump_line), format, copy);
        va_end(copy);
        ++jump_hits;
    } else if (stream == stderr &&
               strncmp(format, "[idle-jump-taken]", 17u) == 0) {
        va_list copy;
        va_copy(copy, arguments);
        vsnprintf(taken_line, sizeof(taken_line), format, copy);
        va_end(copy);
        ++taken_hits;
    }
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

#define IDLE_PC 0x80307EF4u
#define IDLE_POLL 0x803F7A30u
#define IDLE_R13 0x803FE0E0u
#define VI_DI0 (DOL_VI_BASE + DOL_VI_DI0_OFF)
// One timebase tick is twelve guest cycles; decrementer 100 expires at
// (100 + 1) * 12 = 1212 cycles with a zero sub-tick remainder.
#define DECREMENTER_CYCLES 1212u
#define ARAM_CYCLES 600u

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

    cpu->exception = PPC_EXC_PROGRAM;
    assert(!ww_host_idle_poll_ready(host));
    cpu->exception = 0u;

    host->dvd_completions.count = 1u;
    assert(!ww_host_idle_poll_ready(host));
    host->dsp.audio_deferred = true;
    host->dsp.deferred_task_started = true;
    assert(ww_host_idle_poll_ready(host));
    host->dvd_callback_active = true;
    assert(!ww_host_idle_poll_ready(host));
    host->dvd_callback_active = false;
    host->dsp.audio_deferred = false;
    host->dsp.deferred_task_started = false;
    host->dvd_completions.count = 0u;

    host->dvd_callback_active = true;
    assert(!ww_host_idle_poll_ready(host));
    host->dvd_callback_active = false;

    cpu->gpr[13] = IDLE_R13 + 4u;
    assert(!ww_host_idle_poll_ready(host));
    cpu->gpr[13] = IDLE_R13;

    // The coarse "aliases overlap MEM1" flag alone must not refuse: the first
    // runtime REL scratch alias raises it while the poll word stays real. A
    // covering alias still refuses.
    g_ppc_guest_aliases_overlap_mem1 = true;
    assert(ww_host_idle_poll_ready(host));
    assert(ppc_guest_alias_add(0x803F7A30u, 4u, NULL));
    assert(!ww_host_idle_poll_ready(host));
    ppc_guest_alias_clear();
    g_ppc_guest_aliases_overlap_mem1 = false;
    assert(ww_host_idle_poll_ready(host));

    host->scheduler_idle_observed = false;
    assert(!ww_host_idle_poll_ready(host));
    host->scheduler_idle_observed = true;

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

    // The periodic VI retrace is the deadline when nothing is nearer.
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

    // The event's turn is no idle turn.
    cpu->pc = 0x00000500u;
    assert(!ww_host_idle_poll_ready(host));
    bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
    bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
    assert(cpu->cycle_budget == 256);

    // A nearer decrementer clamps the jump with the VI still enabled.
    cpu->pc = IDLE_PC;
    cpu->exception = 0u;
    host->clock_remainder = 0u;
    host->decrementer = 100u;
    host->decrementer_pending = false;
    assert(ww_host_idle_poll_ready(host));
    bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
    bluewake_cycle_domain_set_idle_turn(&host->cycles, true);
    bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
    assert(cpu->cycle_budget == DECREMENTER_CYCLES);
    assert(cpu->cycle_deadline_active == 1u);
    ww_host_note_idle_grant(host);
    assert(jump_hits == 1u);
    assert(strstr(jump_line, "src=2") != NULL);
    assert(strstr(jump_line, "budget=1212") != NULL);
    assert(strstr(jump_line, "dist=1212") != NULL);
    cpu->downcount = -(s64)cpu->cycle_budget;
    const u64 elapsed = bluewake_cycle_domain_end_turn(&host->cycles, cpu);
    assert(elapsed == DECREMENTER_CYCLES);
    ww_host_note_idle_taken(host, elapsed);
    assert(taken_hits == 1u);
    assert(strstr(taken_line, "elapsed=1212") != NULL);
    assert(host->decrementer_pending);
    assert(host->vi_retraces == 1u);

    // A nearer ARAM transfer clamps it too.
    host->decrementer_pending = false;
    host->decrementer = UINT32_MAX;
    host->dsp.aram_cycles_remaining = ARAM_CYCLES;
    assert(ww_host_idle_poll_ready(host));
    bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
    bluewake_cycle_domain_set_idle_turn(&host->cycles, true);
    bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
    assert(cpu->cycle_budget == ARAM_CYCLES);
    assert(cpu->cycle_deadline_active == 1u);
    cpu->downcount = -(s64)cpu->cycle_budget;
    assert(bluewake_cycle_domain_end_turn(&host->cycles, cpu) == ARAM_CYCLES);
    assert(host->dsp.aram_cycles_remaining == 0u);
    assert(host->vi_retraces == 1u);

    // With deferred audio the in-flight ARAM transfer belongs to the same
    // no-op audio path, so it must not clamp the idle span: the turn covers
    // the full VI period and the transfer completes inside it.
    host->dsp.audio_deferred = true;
    host->dsp.deferred_task_started = true;
    host->dsp.aram_cycles_remaining = ARAM_CYCLES;
    assert(ww_dsp_interface_deadline(&host->dsp) == ARAM_CYCLES);
    assert(ww_dsp_interface_deadline_excluding_aram(&host->dsp) == UINT64_MAX);
    assert(ww_host_idle_poll_ready(host));
    bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
    bluewake_cycle_domain_set_idle_turn(&host->cycles, true);
    bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
    const u64 to_vi = GUEST_CPU_CYCLES_PER_VI_RETRACE - DECREMENTER_CYCLES - ARAM_CYCLES;
    assert(cpu->cycle_budget == to_vi);
    assert(cpu->cycle_deadline_active == 1u);
    cpu->downcount = -(s64)cpu->cycle_budget;
    assert(bluewake_cycle_domain_end_turn(&host->cycles, cpu) == to_vi);
    assert(host->dsp.aram_cycles_remaining == 0u);
    assert(host->vi_retraces == 2u);
    host->dsp.audio_deferred = false;
    host->dsp.deferred_task_started = false;

    // The grant decides whether this line belongs to a jump, not the charge:
    // a turn that leaves early still reports the jump it was given.
    host->decrementer_pending = false;
    host->decrementer = 74u;
    assert(ww_host_idle_poll_ready(host));
    bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
    bluewake_cycle_domain_set_idle_turn(&host->cycles, true);
    bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
    assert(cpu->cycle_budget == 900);
    ww_host_note_idle_grant(host);
    assert(jump_hits == 2u);
    cpu->downcount = -200;
    assert(bluewake_cycle_domain_end_turn(&host->cycles, cpu) == 200u);
    ww_host_note_idle_taken(host, 200u);
    assert(taken_hits == 2u);
    assert(strstr(taken_line, "elapsed=200") != NULL);

    // A jump that crosses an edge rebudgets from the remaining distance; when
    // that is inside the cap, the live grant flag clears. The taken line must
    // still commit the jump captured at grant time.
    host->decrementer_pending = false;
    host->decrementer = 74u;
    host->clock_remainder = 0u;
    assert(ww_host_idle_poll_ready(host));
    bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
    bluewake_cycle_domain_set_idle_turn(&host->cycles, true);
    bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
    assert(cpu->cycle_budget == 900);
    ww_host_note_idle_grant(host);
    assert(jump_hits == 3u);
    cpu->downcount = -700;
    assert(bluewake_cycle_domain_flush(&host->cycles, cpu) == 700u);
    assert(!host->cycles.last_idle_grant);
    cpu->downcount = -5;
    assert(bluewake_cycle_domain_end_turn(&host->cycles, cpu) == 5u);
    ww_host_note_idle_taken(host, 5u);
    assert(taken_hits == 3u);
    assert(strstr(taken_line, "elapsed=5") != NULL);
    return 0;
}
