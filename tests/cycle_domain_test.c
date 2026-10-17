#ifdef NDEBUG
#undef NDEBUG
#endif
#include "cycle_domain.h"

#include <assert.h>
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

static u64 deadline(const CPUState* cpu, void* user) {
    (void)cpu;
    Fixture* fixture = user;
    fixture->deadline_calls++;
    return fixture->deadline;
}

static BluewakeCycleDomain* moving_domain;
static u64 moving_base;

static u64 moving_deadline(const CPUState* cpu, void* user) {
    (void)cpu;
    (void)user;
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

    cpu.downcount = -8192;
    assert(bluewake_cycle_domain_end_turn(&domain, &cpu) == 8192u);
    assert(domain.absolute_cycles == 8192u);
    assert(fixture.advanced == 8192u && fixture.advances == 1u);
    assert(cpu.timebase == 682u);
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

// OFF is the parent: no idle turn, or an idle turn with no configured advance,
// keeps the 256-cycle budget and never looks further than the deadline.
static void test_idle_advance_off_keeps_the_standard_budget(void) {
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

    bluewake_cycle_domain_set_idle_advance(&domain, 0);
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

int main(void) {
    test_idle_advance_reaches_the_deadline_in_one_turn();
    test_idle_advance_clamps_at_the_event();
    test_idle_advance_off_keeps_the_standard_budget();
    test_idle_advance_requires_a_known_deadline();
    test_idle_advance_is_bounded_and_repeatable();
    test_idle_turn_does_not_leak_into_the_next_turn();

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
