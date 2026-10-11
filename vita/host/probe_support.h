#ifndef WW05_PROBE_SUPPORT_H
#define WW05_PROBE_SUPPORT_H

#include "boot_host.h"
#include <assert.h>

static inline void charge_cycles(WwHost* host, u64 cycles) {
    assert(cycles <= INT64_MAX);
    bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
    bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
    host->cpu.downcount = -(s64)cycles;
    bluewake_cycle_domain_end_turn(&host->cycles, &host->cpu);
}

#endif
