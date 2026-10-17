#include "cycle_domain.h"

#include <string.h>
// BLUEWAKE_CREDIT_CENSUS. The emitted body's residual cost is the CPUState traffic
// its yield discipline forces, and the design that removes it - the prepaid twin - is
// refused by the route because its turn count rose 2.8x with the guest cycles
// identical. The host's own view of a turn is what flush_elapsed credits, so this
// counts that: turns, zero-credit turns, the credited cycles, and the turns whose
// credit exceeded the budget they were handed. Inert unless the host asks for it.
bool g_cycle_credit_census;
u64 g_cycle_credit_calls;
u64 g_cycle_credit_zero;
u64 g_cycle_credit_sum;
u64 g_cycle_credit_max;
u64 g_cycle_credit_over_budget;

static s64 bounded_budget(const BluewakeCycleDomain* domain,
                          const CPUState* cpu, u32* deadline_active,
                          s64* deadline_budget) {
    s64 budget = domain->cap > 0 ? domain->cap : 256;
    bool bounded_by_deadline = false;
    u64 distance = UINT64_MAX;
    if (domain->deadline != NULL) {
        distance = domain->deadline(cpu, domain->user);
        if (domain->dynamic_near_cap > 0 &&
            distance <= domain->dynamic_threshold)
            budget = domain->dynamic_near_cap;
        // An idle turn may look past the fixed cap, never past the nearest
        // deadline and never past the idle cap (a bounded catch-up).
        if (domain->idle_turn && domain->idle_advance_cap > 0 &&
            distance != UINT64_MAX) {
            const s64 idle_budget =
                distance > (u64)domain->idle_advance_cap
                    ? domain->idle_advance_cap
                    : (s64)distance;
            if (idle_budget > budget)
                budget = idle_budget;
        }
        if (distance <= (u64)budget) {
            budget = distance > 0u ? (s64)distance : 1;
            bounded_by_deadline = true;
        }
    }
    if (deadline_active != NULL)
        *deadline_active = bounded_by_deadline ? 1u : 0u;
    if (deadline_budget != NULL) {
        if (distance == UINT64_MAX)
            *deadline_budget = 0;
        else if (distance > (u64)INT64_MAX)
            *deadline_budget = INT64_MAX;
        else
            *deadline_budget = distance > 0u ? (s64)distance : 1;
    }
    return budget > 0 ? budget : 1;
}

void bluewake_cycle_domain_init(BluewakeCycleDomain* domain, s64 cap,
                                BluewakeCycleAdvanceFn advance,
                                BluewakeCycleDeadlineFn deadline, void* user) {
    if (domain == NULL)
        return;
    memset(domain, 0, sizeof(*domain));
    domain->cap = cap > 0 ? cap : 256;
    domain->advance = advance;
    domain->deadline = deadline;
    domain->user = user;
}

void bluewake_cycle_domain_set_dynamic_cap(BluewakeCycleDomain* domain,
                                           s64 near_cap, u64 threshold) {
    if (domain == NULL)
        return;
    domain->dynamic_near_cap = near_cap > 0 ? near_cap : 0;
    domain->dynamic_threshold = threshold;
}

void bluewake_cycle_domain_set_idle_advance(BluewakeCycleDomain* domain,
                                            s64 cap) {
    if (domain == NULL)
        return;
    domain->idle_advance_cap = cap > 0 ? cap : 0;
}

void bluewake_cycle_domain_set_idle_turn(BluewakeCycleDomain* domain,
                                         bool idle) {
    if (domain == NULL)
        return;
    domain->idle_turn = idle;
}

void bluewake_cycle_domain_begin_turn(BluewakeCycleDomain* domain,
                                      CPUState* cpu) {
    if (domain == NULL || cpu == NULL)
        return;
    domain->dispatch_cycles = 0u;
    // An idle turn is a per-turn decision: never carry one into the next turn.
    domain->idle_turn = false;
    cpu->downcount = 0;
    // No guest code can run until prepare_dispatch establishes the exact
    // deadline. Keep any accidentally unprepared dispatch fail-closed.
    cpu->cycle_budget = 1;
    cpu->cycle_deadline_active = 1u;
    cpu->cycle_deadline_budget = 1;
}

void bluewake_cycle_domain_prepare_dispatch(BluewakeCycleDomain* domain,
                                             CPUState* cpu) {
    if (domain == NULL || cpu == NULL)
        return;
    (void)bluewake_cycle_domain_flush(domain, cpu);
    cpu->cycle_budget = bounded_budget(
        domain, cpu, &cpu->cycle_deadline_active,
        &cpu->cycle_deadline_budget);
}

static u64 flush_elapsed(BluewakeCycleDomain* domain, CPUState* cpu,
                         bool rebudget) {
    if (domain == NULL || cpu == NULL)
        return 0u;
    if (cpu->downcount >= 0) {
        return 0u;
    }

    const u64 elapsed = (u64)(-(cpu->downcount + 1)) + 1u;
    cpu->downcount = 0;
    domain->absolute_cycles += elapsed;
    domain->dispatch_cycles += elapsed;
    if (domain->advance != NULL)
        domain->advance(cpu, elapsed, domain->user);
    if (rebudget) {
        cpu->cycle_budget = bounded_budget(
            domain, cpu, &cpu->cycle_deadline_active,
            &cpu->cycle_deadline_budget);
    }
    return elapsed;
}

u64 bluewake_cycle_domain_flush(BluewakeCycleDomain* domain, CPUState* cpu) {
    return flush_elapsed(domain, cpu, true);
}

u64 bluewake_cycle_domain_end_turn(BluewakeCycleDomain* domain,
                                   CPUState* cpu) {
    return flush_elapsed(domain, cpu, false);
}

u64 bluewake_cycle_domain_observe(BluewakeCycleDomain* domain, CPUState* cpu,
                                  u32 unexecuted_suffix) {
    if (domain == NULL || cpu == NULL || cpu->downcount >= 0)
        return 0u;

    const u64 charged = (u64)(-(cpu->downcount + 1)) + 1u;
    const u64 suffix = unexecuted_suffix < charged
                           ? (u64)unexecuted_suffix
                           : charged;
    const u64 elapsed = charged - suffix;
    cpu->downcount = -(s64)suffix;
    domain->absolute_cycles += elapsed;
    domain->dispatch_cycles += elapsed;
    if (domain->advance != NULL && elapsed != 0u)
        domain->advance(cpu, elapsed, domain->user);
    cpu->cycle_budget = bounded_budget(
        domain, cpu, &cpu->cycle_deadline_active,
        &cpu->cycle_deadline_budget);
    return elapsed;
}

void bluewake_cycle_domain_rebudget(BluewakeCycleDomain* domain,
                                    CPUState* cpu) {
    if (domain == NULL || cpu == NULL)
        return;
    cpu->cycle_budget = bounded_budget(
        domain, cpu, &cpu->cycle_deadline_active,
        &cpu->cycle_deadline_budget);
}

void bluewake_cycle_domain_write_timebase(BluewakeCycleDomain* domain,
                                          CPUState* cpu, u16 spr, u32 value) {
    if (domain == NULL || cpu == NULL || (spr != 284u && spr != 285u))
        return;

    (void)bluewake_cycle_domain_observe(domain, cpu,
                                        cpu->cycle_observation_suffix);
    if (spr == 284u) {
        cpu->timebase = (cpu->timebase & 0xFFFFFFFF00000000ull) | value;
    } else {
        cpu->timebase = ((u64)value << 32) | (cpu->timebase & 0xFFFFFFFFull);
    }
}
