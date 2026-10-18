#ifdef NDEBUG
#undef NDEBUG
#endif
#include "cycle_domain.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct Fixture {
    u64 advanced;
    u64 deadline;
    unsigned advances;
    unsigned deadline_calls;
} Fixture;

static BluewakeCycleDomain* observation_domain;

static u32 observe_timebase(CPUState* cpu, u16 spr, u32 cia) {
    (void)cia;
    (void)bluewake_cycle_domain_observe(
        observation_domain, cpu, cpu->cycle_observation_suffix);
    return spr == 269u ? (u32)(cpu->timebase >> 32) : (u32)cpu->timebase;
}

static void advance(CPUState* cpu, u64 cycles, void* user) {
    Fixture* fixture = user;
    fixture->advanced += cycles;
    fixture->advances++;
    cpu->timebase += cycles / 12u;
}

static u64 deadline(const CPUState* cpu, u32* source, void* user) {
    (void)cpu;
    Fixture* fixture = user;
    fixture->deadline_calls++;
    if (source != NULL)
        *source = BLUEWAKE_DEADLINE_VI;
    return fixture->deadline;
}

static BluewakeCycleDomain* moving_domain;
static u64 moving_base;

static u64 moving_deadline(const CPUState* cpu, u32* source, void* user) {
    (void)cpu;
    (void)user;
    if (source != NULL)
        *source = BLUEWAKE_DEADLINE_DEC_EXPIRY;
    return moving_domain->absolute_cycles >= moving_base
               ? 0u
               : moving_base - moving_domain->absolute_cycles;
}

// No event before the deadline: one turn covers the whole distance.
static void test_idle_advance_reaches_the_deadline_in_one_turn(void) {
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    Fixture fixture = {.deadline = 8192u};
    BluewakeCycleDomain domain;
    bluewake_cycle_domain_init(&domain, 256, advance, deadline, &fixture);
    bluewake_cycle_domain_set_idle_advance(&domain, 8192);

    bluewake_cycle_domain_begin_turn(&domain, &cpu);
    bluewake_cycle_domain_set_idle_turn(&domain, true);
    bluewake_cycle_domain_prepare_dispatch(&domain, &cpu);
    assert(cpu.cycle_budget == 8192);
    assert(cpu.cycle_deadline_active == 1u);
    assert(cpu.cycle_deadline_budget == 8192);
    assert(domain.last_idle_grant);
    assert(domain.last_budget == 8192u && domain.last_distance == 8192u);
    assert(domain.last_source == BLUEWAKE_DEADLINE_VI);

    cpu.downcount = -8192;
    assert(bluewake_cycle_domain_end_turn(&domain, &cpu) == 8192u);
    assert(domain.absolute_cycles == 8192u);
    assert(fixture.advanced == 8192u && fixture.advances == 1u);
    assert(cpu.timebase == 682u);
}

// The idle cap is below the deadline: the grant is capped and attributed to CAP.
static void test_idle_advance_reports_the_idle_cap_as_the_source(void) {
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    Fixture fixture = {.deadline = 8192u};
    BluewakeCycleDomain domain;
    bluewake_cycle_domain_init(&domain, 256, advance, deadline, &fixture);
    bluewake_cycle_domain_set_idle_advance(&domain, 4096);

    bluewake_cycle_domain_begin_turn(&domain, &cpu);
    bluewake_cycle_domain_set_idle_turn(&domain, true);
    bluewake_cycle_domain_prepare_dispatch(&domain, &cpu);
    assert(cpu.cycle_budget == 4096);
    assert(cpu.cycle_deadline_active == 0u);
    assert(domain.last_idle_grant);
    assert(domain.last_source == BLUEWAKE_DEADLINE_CAP);
    assert(domain.last_budget == 4096u);
    assert(domain.last_distance == 8192u);
    assert(domain.last_budget < domain.last_distance);
}

// An event lands inside the distance: the turn stops on it and the device sees
// exactly the event cycle, not the idle cap.
static void test_idle_advance_clamps_at_the_event(void) {
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    Fixture fixture = {.deadline = 900u};
    BluewakeCycleDomain domain;
    bluewake_cycle_domain_init(&domain, 256, advance, deadline, &fixture);
    bluewake_cycle_domain_set_idle_advance(&domain, 8192);

    bluewake_cycle_domain_begin_turn(&domain, &cpu);
    bluewake_cycle_domain_set_idle_turn(&domain, true);
    bluewake_cycle_domain_prepare_dispatch(&domain, &cpu);
    assert(cpu.cycle_budget == 900);
    assert(cpu.cycle_deadline_active == 1u);
    assert(cpu.cycle_deadline_budget == 900);

    cpu.downcount = -900;
    assert(bluewake_cycle_domain_end_turn(&domain, &cpu) == 900u);
    assert(domain.absolute_cycles == 900u);
    assert(fixture.advanced == 900u && fixture.advances == 1u);
}

// Without an idle mark the parent budget stands.
static void test_idle_advance_without_a_mark_keeps_the_standard_budget(void) {
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    Fixture fixture = {.deadline = 8192u};
    BluewakeCycleDomain domain;
    bluewake_cycle_domain_init(&domain, 256, advance, deadline, &fixture);
    bluewake_cycle_domain_set_idle_advance(&domain, 8192);

    bluewake_cycle_domain_begin_turn(&domain, &cpu);
    bluewake_cycle_domain_prepare_dispatch(&domain, &cpu);
    assert(cpu.cycle_budget == 256);
    assert(cpu.cycle_deadline_active == 0u);
    assert(cpu.cycle_deadline_budget == 8192);
    assert(!domain.last_idle_grant);
}

// An idle mark without a configured advance is still the parent budget.
static void test_idle_advance_without_a_configured_cap_keeps_the_standard_budget(void) {
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    Fixture fixture = {.deadline = 8192u};
    BluewakeCycleDomain domain;
    bluewake_cycle_domain_init(&domain, 256, advance, deadline, &fixture);

    bluewake_cycle_domain_begin_turn(&domain, &cpu);
    bluewake_cycle_domain_set_idle_turn(&domain, true);
    bluewake_cycle_domain_prepare_dispatch(&domain, &cpu);
    assert(cpu.cycle_budget == 256);
}

// With no known deadline the advance must not run away.
static void test_idle_advance_requires_a_known_deadline(void) {
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    Fixture fixture = {.deadline = UINT64_MAX};
    BluewakeCycleDomain domain;
    bluewake_cycle_domain_init(&domain, 256, advance, deadline, &fixture);
    bluewake_cycle_domain_set_idle_advance(&domain, 8192);

    bluewake_cycle_domain_begin_turn(&domain, &cpu);
    bluewake_cycle_domain_set_idle_turn(&domain, true);
    bluewake_cycle_domain_prepare_dispatch(&domain, &cpu);
    assert(cpu.cycle_budget == 256);
    assert(cpu.cycle_deadline_active == 0u);
    assert(cpu.cycle_deadline_budget == 0);
}

// The cap bounds one turn; repeating turns still lands exactly on the event,
// never past it and never with a growing overshoot.
static void test_idle_advance_is_bounded_and_repeatable(void) {
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    Fixture fixture = {0};
    moving_base = 20000u;
    BluewakeCycleDomain domain;
    bluewake_cycle_domain_init(&domain, 256, advance, moving_deadline, &fixture);
    bluewake_cycle_domain_set_idle_advance(&domain, 8192);
    moving_domain = &domain;

    const u64 expected[] = {8192u, 8192u, 3616u};
    u64 advanced = 0u;
    for (unsigned turn = 0u; turn < 3u; ++turn) {
        bluewake_cycle_domain_begin_turn(&domain, &cpu);
        bluewake_cycle_domain_set_idle_turn(&domain, true);
        bluewake_cycle_domain_prepare_dispatch(&domain, &cpu);
        assert((u64)cpu.cycle_budget == expected[turn]);
        cpu.downcount = -cpu.cycle_budget;
        advanced += bluewake_cycle_domain_end_turn(&domain, &cpu);
    }
    assert(advanced == moving_base);
    assert(domain.absolute_cycles == moving_base);
    assert(fixture.advanced == moving_base && fixture.advances == 3u);
}

// A zero request keeps the parent's fixed cap; only a positive POC value
// replaces it.
static void test_turn_cap_selection_keeps_the_parent_default(void) {
    assert(bluewake_cycle_domain_select_cap(0) == 256);
    assert(bluewake_cycle_domain_select_cap(-1) == 256);

    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    Fixture fixture = {.deadline = 100000u};
    BluewakeCycleDomain domain;
    bluewake_cycle_domain_init(&domain,
                               bluewake_cycle_domain_select_cap(0),
                               advance, deadline, &fixture);
    assert(domain.cap == 256);
}

// The POC package asks for 4096 cycles per turn.
static void test_turn_cap_selection_accepts_the_poc_cap(void) {
    assert(bluewake_cycle_domain_select_cap(4096) == 4096);

    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    Fixture fixture = {.deadline = 100000u};
    BluewakeCycleDomain domain;
    bluewake_cycle_domain_init(&domain,
                               bluewake_cycle_domain_select_cap(4096),
                               advance, deadline, &fixture);
    assert(domain.cap == 4096);
}

// A device event nearer than the POC cap still ends the turn exactly on it.
static void test_poc_turn_cap_still_stops_at_the_device_deadline(void) {
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    Fixture fixture = {.deadline = 900u};
    BluewakeCycleDomain domain;
    bluewake_cycle_domain_init(&domain,
                               bluewake_cycle_domain_select_cap(4096),
                               advance, deadline, &fixture);

    bluewake_cycle_domain_begin_turn(&domain, &cpu);
    bluewake_cycle_domain_prepare_dispatch(&domain, &cpu);
    assert(cpu.cycle_budget == 900);
    assert(cpu.cycle_deadline_active == 1u);
    assert(cpu.cycle_deadline_budget == 900);
    assert(domain.last_source == BLUEWAKE_DEADLINE_VI);
}

// With no nearer device event the POC cap bounds the turn and is attributed
// to the cap, not to a deadline.
static void test_poc_turn_cap_bounds_a_turn_without_a_nearer_event(void) {
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    Fixture fixture = {.deadline = 100000u};
    BluewakeCycleDomain domain;
    bluewake_cycle_domain_init(&domain,
                               bluewake_cycle_domain_select_cap(4096),
                               advance, deadline, &fixture);

    bluewake_cycle_domain_begin_turn(&domain, &cpu);
    bluewake_cycle_domain_prepare_dispatch(&domain, &cpu);
    assert(cpu.cycle_budget == 4096);
    assert(cpu.cycle_deadline_active == 0u);
    assert(cpu.cycle_deadline_budget == 100000);
    assert(domain.last_source == BLUEWAKE_DEADLINE_CAP);
}

// Repeating bounded turns lands exactly on the event: never past it and never
// with a growing overshoot.
static void test_poc_turn_cap_is_bounded_and_repeatable(void) {
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    Fixture fixture = {0};
    moving_base = 20000u;
    BluewakeCycleDomain domain;
    bluewake_cycle_domain_init(&domain,
                               bluewake_cycle_domain_select_cap(4096),
                               advance, moving_deadline, &fixture);
    moving_domain = &domain;

    const u64 expected[] = {4096u, 4096u, 4096u, 4096u, 3616u};
    u64 advanced = 0u;
    for (unsigned turn = 0u; turn < 5u; ++turn) {
        bluewake_cycle_domain_begin_turn(&domain, &cpu);
        bluewake_cycle_domain_prepare_dispatch(&domain, &cpu);
        assert((u64)cpu.cycle_budget == expected[turn]);
        cpu.downcount = -cpu.cycle_budget;
        advanced += bluewake_cycle_domain_end_turn(&domain, &cpu);
    }
    assert(advanced == moving_base);
    assert(domain.absolute_cycles == moving_base);
    assert(fixture.advanced == moving_base && fixture.advances == 5u);
}

// One idle turn must not leak into the next: begin_turn clears it and the
// standard 256-cycle budget comes back.
static void test_idle_turn_does_not_leak_into_the_next_turn(void) {
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    Fixture fixture = {.deadline = 100000u};
    BluewakeCycleDomain domain;
    bluewake_cycle_domain_init(&domain, 256, advance, deadline, &fixture);
    bluewake_cycle_domain_set_idle_advance(&domain, 8192);

    bluewake_cycle_domain_begin_turn(&domain, &cpu);
    bluewake_cycle_domain_set_idle_turn(&domain, true);
    bluewake_cycle_domain_prepare_dispatch(&domain, &cpu);
    assert(cpu.cycle_budget == 8192);

    bluewake_cycle_domain_begin_turn(&domain, &cpu);
    bluewake_cycle_domain_prepare_dispatch(&domain, &cpu);
    assert(cpu.cycle_budget == 256);
    assert(cpu.cycle_deadline_active == 0u);
}

// A NULL domain is inert: the public entries return before any dereference.
static void test_invalid_domain_null_is_inert(void) {
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.cycle_budget = 7;
    cpu.cycle_deadline_active = 4u;
    cpu.cycle_deadline_budget = 5;

    bluewake_cycle_domain_begin_turn(NULL, &cpu);
    bluewake_cycle_domain_prepare_dispatch(NULL, &cpu);
    bluewake_cycle_domain_rebudget(NULL, &cpu);

    assert(cpu.cycle_budget == 7);
    assert(cpu.cycle_deadline_active == 4u);
    assert(cpu.cycle_deadline_budget == 5);
}

// A pointer below the first page cannot be a domain: refuse it and grant the
// safe single-cycle budget.
static void test_invalid_domain_low_pointer_fails_closed(void) {
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    BluewakeCycleDomain* const domain =
        (BluewakeCycleDomain*)(uintptr_t)0x40u;

    bluewake_cycle_domain_rebudget(domain, &cpu);

    assert(cpu.cycle_budget == 1);
    assert(cpu.cycle_deadline_active == 1u);
    assert(cpu.cycle_deadline_budget == 1);
}

// A misaligned pointer cannot be this struct either, and is refused the same way.
static void test_invalid_domain_misaligned_pointer_fails_closed(void) {
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    BluewakeCycleDomain* const domain =
        (BluewakeCycleDomain*)(uintptr_t)(0x10000u + 1u);

    bluewake_cycle_domain_rebudget(domain, &cpu);

    assert(cpu.cycle_budget == 1);
    assert(cpu.cycle_deadline_active == 1u);
    assert(cpu.cycle_deadline_budget == 1);
}

// A usable domain still returns the budget it was configured with.
static void test_valid_domain_returns_the_configured_budget(void) {
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    Fixture fixture = {.deadline = 100000u};
    BluewakeCycleDomain domain;
    bluewake_cycle_domain_init(&domain, 256, advance, deadline, &fixture);

    bluewake_cycle_domain_rebudget(&domain, &cpu);

    assert(cpu.cycle_budget == 256);
    assert(cpu.cycle_deadline_active == 0u);
    assert(cpu.cycle_deadline_budget == 100000);
    assert(domain.last_budget == 256u);
}

int main(void) {
    test_idle_advance_reaches_the_deadline_in_one_turn();
    test_idle_advance_clamps_at_the_event();
    test_idle_advance_reports_the_idle_cap_as_the_source();
    test_idle_advance_without_a_mark_keeps_the_standard_budget();
    test_idle_advance_without_a_configured_cap_keeps_the_standard_budget();
    test_idle_advance_requires_a_known_deadline();
    test_idle_advance_is_bounded_and_repeatable();
    test_turn_cap_selection_keeps_the_parent_default();
    test_turn_cap_selection_accepts_the_poc_cap();
    test_poc_turn_cap_still_stops_at_the_device_deadline();
    test_poc_turn_cap_bounds_a_turn_without_a_nearer_event();
    test_poc_turn_cap_is_bounded_and_repeatable();
    test_idle_turn_does_not_leak_into_the_next_turn();
    test_invalid_domain_null_is_inert();
    test_invalid_domain_low_pointer_fails_closed();
    test_invalid_domain_misaligned_pointer_fails_closed();
    test_valid_domain_returns_the_configured_budget();

    CPUState cpu;
    Fixture fixture = {.deadline = 900u};
    BluewakeCycleDomain domain;
    memset(&cpu, 0, sizeof(cpu));

    bluewake_cycle_domain_init(&domain, 1024, advance, deadline, &fixture);
    bluewake_cycle_domain_begin_turn(&domain, &cpu);
    assert(cpu.cycle_budget == 1);
    assert(cpu.cycle_deadline_active == 1u);
    assert(cpu.cycle_deadline_budget == 1);
    assert(fixture.deadline_calls == 0u);

    bluewake_cycle_domain_prepare_dispatch(&domain, &cpu);
    assert(cpu.cycle_budget == 900);
    assert(cpu.cycle_deadline_active == 1u);
    assert(cpu.cycle_deadline_budget == 900);
    assert(fixture.deadline_calls == 1u);

    cpu.downcount = -144;
    assert(bluewake_cycle_domain_flush(&domain, &cpu) == 144u);
    assert(cpu.downcount == 0);
    assert(domain.absolute_cycles == 144u);
    assert(domain.dispatch_cycles == 144u);
    assert(fixture.advanced == 144u);
    assert(fixture.advances == 1u);
    assert(cpu.timebase == 12u);

    cpu.downcount = -30;
    assert(bluewake_cycle_domain_observe(&domain, &cpu, 11u) == 19u);
    assert(cpu.downcount == -11);
    assert(domain.absolute_cycles == 163u);
    assert(domain.dispatch_cycles == 163u);
    assert(fixture.advanced == 163u);
    assert(bluewake_cycle_domain_flush(&domain, &cpu) == 11u);
    assert(domain.absolute_cycles == 174u);

    assert(fixture.advances == 3u); // flush, observe, remaining suffix
    assert(bluewake_cycle_domain_flush(&domain, &cpu) == 0u);
    assert(fixture.advances == 3u); // an empty flush does not advance devices

    fixture.deadline = 0u;
    bluewake_cycle_domain_rebudget(&domain, &cpu);
    assert(cpu.cycle_budget == 1);
    assert(cpu.cycle_deadline_budget == 1);

    fixture.deadline = 4096u;
    bluewake_cycle_domain_rebudget(&domain, &cpu);
    assert(cpu.cycle_budget == 1024);
    assert(cpu.cycle_deadline_active == 0u);
    assert(cpu.cycle_deadline_budget == 4096);

    bluewake_cycle_domain_set_dynamic_cap(&domain, 256, 1024u);
    fixture.deadline = 900u;
    bluewake_cycle_domain_rebudget(&domain, &cpu);
    assert(cpu.cycle_budget == 256);
    assert(cpu.cycle_deadline_budget == 900);
    fixture.deadline = 200u;
    bluewake_cycle_domain_rebudget(&domain, &cpu);
    assert(cpu.cycle_budget == 200);
    assert(cpu.cycle_deadline_budget == 200);
    fixture.deadline = 4096u;
    bluewake_cycle_domain_rebudget(&domain, &cpu);
    assert(cpu.cycle_budget == 1024);
    assert(cpu.cycle_deadline_budget == 4096);
    bluewake_cycle_domain_set_dynamic_cap(&domain, 0, 0u);

    bluewake_cycle_domain_begin_turn(&domain, &cpu);
    assert(domain.dispatch_cycles == 0u);
    assert(domain.absolute_cycles == 174u);
    assert(cpu.cycle_budget == 1);
    assert(cpu.cycle_deadline_active == 1u);
    assert(cpu.cycle_deadline_budget == 1);

    cpu.downcount = -12;
    bluewake_cycle_domain_prepare_dispatch(&domain, &cpu);
    assert(domain.dispatch_cycles == 12u);
    assert(domain.absolute_cycles == 186u);
    assert(cpu.downcount == 0);

    observation_domain = &domain;
    cpu.spr_read = observe_timebase;
    cpu.downcount = -24;
    cpu.cycle_observation_suffix = 6u;
    assert(ppc_mftb(&cpu, 268u, 0x80001000u) == 15u);
    assert(cpu.downcount == -6);
    assert(domain.absolute_cycles == 204u);
    assert(bluewake_cycle_domain_flush(&domain, &cpu) == 6u);

    const unsigned deadline_calls = fixture.deadline_calls;
    cpu.downcount = -7;
    cpu.cycle_budget = 333;
    assert(bluewake_cycle_domain_end_turn(&domain, &cpu) == 7u);
    assert(fixture.deadline_calls == deadline_calls);
    assert(cpu.cycle_budget == 333);

    memset(&cpu, 0, sizeof(cpu));
    memset(&fixture, 0, sizeof(fixture));
    fixture.deadline = UINT64_MAX;
    bluewake_cycle_domain_init(&domain, 1024, advance, deadline, &fixture);
    cpu.timebase = 0x1122334455667788ull;
    cpu.downcount = -30;
    cpu.cycle_observation_suffix = 6u;
    bluewake_cycle_domain_write_timebase(&domain, &cpu, 284u, 0xAABBCCDDu);
    assert(domain.absolute_cycles == 24u);
    assert(fixture.advanced == 24u);
    assert(cpu.downcount == -6);
    assert(cpu.timebase == 0x11223344AABBCCDDull);
    return 0;
}
