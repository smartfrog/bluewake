#include "fp_runtime.h"
#include "direct_calls.h"
#include <fenv.h>
#include <string.h>

int fixture_dispatch(CPUState* cpu);

typedef struct FpBoundary {
    u32 reject;
    u32 observations;
    u32 mode;
    bool mode_ok;
} FpBoundary;

static bool can_skip(void* user, const CPUState* cpu, u32 address) {
    FpBoundary* probe = user;
    probe->observations++;
    probe->mode_ok = probe->mode_ok && (cpu->fpscr & 7u) == probe->mode;
    return address != probe->reject;
}

int fixture_fp_case(const FpCase* test, u64* actual, int* path, FpObservation observe, void* user) {
    fenv_t saved;
    CPUState cpu;
    *actual = 0;
    *path = 0;
    if (observe && !observe(user, 0))
        return 1;
    if (fegetenv(&saved) != 0 || !cpu_init(&cpu))
        return 1;
    static const bool clear = false;
    static const u32 zero = 0;
    FpBoundary probe = {.reject = test->boundary ? 0x80007108u : 0,
                        .mode = test->mode, .mode_ok = true};
    bool ready = bluewake_composite_direct_calls_v2(true, &clear, &clear, &zero, &zero,
                                                   can_skip, &probe) &&
                 bluewake_composite_edge_filter(true);
    cpu.pc = test->pc;
    cpu.lr = 0x81234564u;
    cpu.gpr[4] = 0x80001000u;
    cpu.gpr[12] = 0x80007000u;
    cpu.cycle_budget = cpu.cycle_deadline_budget = 1000000;
    cpu.msr = PPC_MSR_FP;
    cpu.hid2 = PPC_HID2_PSE | PPC_HID2_LSQE;
    cpu.gqr[0] = test->gqr;
    memcpy(&cpu.fpr[2], &test->a, sizeof test->a);
    memcpy(&cpu.fpr[3], &test->b, sizeof test->b);
    cpu.fpscr = test->mode;
    if (observe && !observe(user, 1))
        ready = false;
    ppc_fpscr_control_updated(&cpu);
    if (observe && !observe(user, 2))
        ready = false;
    if (ready && fixture_dispatch(&cpu)) {
        *path = cpu.pc == (test->boundary ? probe.reject : 0x81234564u) &&
                bw_direct_depth == 0 && probe.mode_ok &&
                (test->pc < 0x80007100u || probe.observations == 2);
        if (test->width == 1)
            *actual = mem_read8(&cpu, cpu.gpr[4]);
        else if (test->width == 2)
            *actual = mem_read16(&cpu, cpu.gpr[4]);
        else
            memcpy(actual, &cpu.fpr[1], sizeof *actual);
    }
    if (observe && !observe(user, 3))
        *path = 0;
    bluewake_composite_direct_calls_v2(false, NULL, NULL, NULL, NULL, NULL, NULL);
    cpu.fpscr = 0;
    ppc_fpscr_control_updated(&cpu);
    cpu_free(&cpu);
    if (fesetenv(&saved) != 0)
        *path = 0;
    return !*path || *actual != test->expected;
}
