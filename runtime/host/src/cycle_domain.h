#ifndef BLUEWAKE_CYCLE_DOMAIN_H
#define BLUEWAKE_CYCLE_DOMAIN_H

#include <stdbool.h>

#include "core/cpu.h"

// Off by default: the parent's fixed-budget turn scheduler. When 1, the host
// may mark a turn that is spinning in the guest scheduler idle loop and the
// domain hands that turn the distance to the next device deadline instead of
// the fixed cap. See bluewake_cycle_domain_set_idle_advance().
#ifndef WW_IDLE_DEADLINE_ADVANCE
#define WW_IDLE_DEADLINE_ADVANCE 0
#endif

// See cycle_domain.c: what the host credits each turn, for the credit census.
extern bool g_cycle_credit_census;
extern u64 g_cycle_credit_calls;
extern u64 g_cycle_credit_zero;
extern u64 g_cycle_credit_sum;
extern u64 g_cycle_credit_max;
extern u64 g_cycle_credit_over_budget;

typedef void (*BluewakeCycleAdvanceFn)(CPUState* cpu, u64 cycles, void* user);
typedef u64 (*BluewakeCycleDeadlineFn)(const CPUState* cpu, void* user);

typedef struct BluewakeCycleDomain {
    u64 absolute_cycles;
    u64 dispatch_cycles;
    s64 cap;
    s64 dynamic_near_cap;
    u64 dynamic_threshold;
    // Idle advance: 0 disables it. A host that knows the guest is spinning in
    // the scheduler idle loop marks the turn and gets min(distance, cap_idle)
    // instead of the fixed cap, so one turn covers a whole wait without
    // executing it. begin_turn clears idle_turn; the host sets it per turn.
    s64 idle_advance_cap;
    bool idle_turn;
    BluewakeCycleAdvanceFn advance;
    BluewakeCycleDeadlineFn deadline;
    void* user;
} BluewakeCycleDomain;

void bluewake_cycle_domain_init(BluewakeCycleDomain* domain, s64 cap,
                                BluewakeCycleAdvanceFn advance,
                                BluewakeCycleDeadlineFn deadline, void* user);
void bluewake_cycle_domain_set_dynamic_cap(BluewakeCycleDomain* domain,
                                           s64 near_cap, u64 threshold);
void bluewake_cycle_domain_set_idle_advance(BluewakeCycleDomain* domain,
                                            s64 cap);
void bluewake_cycle_domain_set_idle_turn(BluewakeCycleDomain* domain,
                                         bool idle);
void bluewake_cycle_domain_begin_turn(BluewakeCycleDomain* domain,
                                      CPUState* cpu);
void bluewake_cycle_domain_prepare_dispatch(BluewakeCycleDomain* domain,
                                             CPUState* cpu);
u64 bluewake_cycle_domain_flush(BluewakeCycleDomain* domain, CPUState* cpu);
u64 bluewake_cycle_domain_end_turn(BluewakeCycleDomain* domain,
                                   CPUState* cpu);
u64 bluewake_cycle_domain_observe(BluewakeCycleDomain* domain, CPUState* cpu,
                                  u32 unexecuted_suffix);
void bluewake_cycle_domain_rebudget(BluewakeCycleDomain* domain,
                                    CPUState* cpu);
void bluewake_cycle_domain_write_timebase(BluewakeCycleDomain* domain,
                                          CPUState* cpu, u16 spr, u32 value);

#endif
