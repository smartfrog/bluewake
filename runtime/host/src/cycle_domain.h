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

// The parent's fixed turn cap. A host may request a different cap (a POC
// package does), but a request of 0 keeps this value.
#define BLUEWAKE_CYCLE_DOMAIN_DEFAULT_CAP 256

// Turns a configured request into the cap the domain will use: a positive
// request stands, anything else keeps the parent default.
s64 bluewake_cycle_domain_select_cap(s64 requested);

// See cycle_domain.c: what the host credits each turn, for the credit census.
extern bool g_cycle_credit_census;
extern u64 g_cycle_credit_calls;
extern u64 g_cycle_credit_zero;
extern u64 g_cycle_credit_sum;
extern u64 g_cycle_credit_max;
extern u64 g_cycle_credit_over_budget;

typedef void (*BluewakeCycleAdvanceFn)(CPUState* cpu, u64 cycles, void* user);
// Reports the distance to the nearest device deadline and, in *source, which
// term of that minimum won (BluewakeDeadlineSource).
typedef u64 (*BluewakeCycleDeadlineFn)(const CPUState* cpu, u32* source,
                                       void* user);

// Which limit bounded a turn's granted budget. Stable values: they are printed
// in the [idle-jump] diagnostic, so a later reader can decode a saved log.
typedef enum BluewakeDeadlineSource {
    BLUEWAKE_DEADLINE_NONE = 0,      // no device deadline at all
    BLUEWAKE_DEADLINE_DEC_PENDING = 1, // a deliverable decrementer is pending
    BLUEWAKE_DEADLINE_DEC_EXPIRY = 2,  // the next decrementer expiry
    BLUEWAKE_DEADLINE_DSP_ARAM = 3,    // an ARAM transfer completing
    BLUEWAKE_DEADLINE_VI = 4,          // the next VI retrace
    BLUEWAKE_DEADLINE_DSP_INIT = 5,    // DSP init progress
    BLUEWAKE_DEADLINE_CAP = 6,         // a cycle cap (fixed or idle)
} BluewakeDeadlineSource;


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
    // Last granted budget and the limit that bounded it, for the host's
    // [idle-jump] diagnostic. Written by prepare_dispatch/observe/rebudget.
    u64 last_budget;
    u64 last_distance;
    u32 last_source;
    bool last_idle_grant;
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
