// SPDX-License-Identifier: GPL-3.0-or-later
// Host contract tests for the boot-time clock lever. No ScePower, no Vita and
// no game data: a recording fake setter exercises the upward-only policy.
#include "vita_clocks.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition, message)                           \
    do {                                                    \
        if (!(condition)) {                                 \
            fprintf(stderr, "FAIL %s: %s\n", name, message); \
            return 1;                                       \
        }                                                   \
    } while (0)

typedef struct {
    int count;
    int axis[BW_VITA_CLOCK_AXES];
    int mhz[BW_VITA_CLOCK_AXES];
    int fail_axis;
} Calls;

static Calls g_calls;

static int record(int axis, int mhz) {
    g_calls.axis[g_calls.count] = axis;
    g_calls.mhz[g_calls.count] = mhz;
    ++g_calls.count;
    return axis == g_calls.fail_axis ? -1 : 0;
}

static void reset_calls(void) {
    memset(&g_calls, 0, sizeof g_calls);
    g_calls.fail_axis = -1;
}

static int case_target_constants(const char* name) {
    const BwVitaClocks max = bw_vita_clocks_max();
    CHECK(max.mhz[BW_VITA_CLOCK_ARM] == 444, "ARM target differs");
    CHECK(max.mhz[BW_VITA_CLOCK_BUS] == 222, "bus target differs");
    CHECK(max.mhz[BW_VITA_CLOCK_GPU] == 222, "GPU target differs");
    CHECK(max.mhz[BW_VITA_CLOCK_XBAR] == 166, "XBAR target differs");
    printf("PASS %s\n", name);
    return 0;
}

static int case_raises_below(const char* name) {
    reset_calls();
    const BwVitaClocks current = {{222, 111, 111, 83}};
    const BwVitaClocks target = bw_vita_clocks_max();
    const BwVitaClockOutcome out = bw_vita_clocks_raise(&current, &target, record);
    CHECK(out.raised_mask == 15, "not every axis was raised");
    CHECK(out.failed_mask == 0, "an axis failed unexpectedly");
    CHECK(g_calls.count == 4, "setter call count differs");
    CHECK(g_calls.axis[0] == BW_VITA_CLOCK_ARM && g_calls.mhz[0] == 444, "ARM call differs");
    CHECK(g_calls.axis[1] == BW_VITA_CLOCK_BUS && g_calls.mhz[1] == 222, "bus call differs");
    CHECK(g_calls.axis[2] == BW_VITA_CLOCK_GPU && g_calls.mhz[2] == 222, "GPU call differs");
    CHECK(g_calls.axis[3] == BW_VITA_CLOCK_XBAR && g_calls.mhz[3] == 166, "XBAR call differs");
    printf("PASS %s\n", name);
    return 0;
}

static int case_noop_at_target(const char* name) {
    reset_calls();
    const BwVitaClocks target = bw_vita_clocks_max();
    const BwVitaClockOutcome out = bw_vita_clocks_raise(&target, &target, record);
    CHECK(out.raised_mask == 0 && out.failed_mask == 0, "already-max device reported a change");
    CHECK(g_calls.count == 0, "setter called at target");
    printf("PASS %s\n", name);
    return 0;
}

static int case_never_lowers(const char* name) {
    reset_calls();
    const BwVitaClocks current = {{500, 300, 300, 300}};
    const BwVitaClocks target = bw_vita_clocks_max();
    const BwVitaClockOutcome out = bw_vita_clocks_raise(&current, &target, record);
    CHECK(out.raised_mask == 0 && out.failed_mask == 0, "above-target device reported a change");
    CHECK(g_calls.count == 0, "setter called to lower a clock");
    printf("PASS %s\n", name);
    return 0;
}

static int case_mixed_axes(const char* name) {
    reset_calls();
    const BwVitaClocks current = {{444, 111, 222, 83}};
    const BwVitaClocks target = bw_vita_clocks_max();
    const BwVitaClockOutcome out = bw_vita_clocks_raise(&current, &target, record);
    CHECK(out.raised_mask == ((1 << BW_VITA_CLOCK_BUS) | (1 << BW_VITA_CLOCK_XBAR)),
          "raised mask differs");
    CHECK(g_calls.count == 2, "setter call count differs");
    CHECK(g_calls.axis[0] == BW_VITA_CLOCK_BUS && g_calls.mhz[0] == 222, "bus call differs");
    CHECK(g_calls.axis[1] == BW_VITA_CLOCK_XBAR && g_calls.mhz[1] == 166, "XBAR call differs");
    printf("PASS %s\n", name);
    return 0;
}

static int case_failure_isolated(const char* name) {
    reset_calls();
    g_calls.fail_axis = BW_VITA_CLOCK_BUS;
    const BwVitaClocks current = {{0, 0, 0, 0}};
    const BwVitaClocks target = bw_vita_clocks_max();
    const BwVitaClockOutcome out = bw_vita_clocks_raise(&current, &target, record);
    CHECK(out.failed_mask == (1 << BW_VITA_CLOCK_BUS), "bus failure not reported");
    CHECK(out.raised_mask == ((1 << BW_VITA_CLOCK_ARM) | (1 << BW_VITA_CLOCK_GPU) |
                              (1 << BW_VITA_CLOCK_XBAR)),
          "a bus failure stopped the other axes");
    CHECK(g_calls.count == 4, "setter call count differs after a failure");
    printf("PASS %s\n", name);
    return 0;
}

static int case_host_is_unsupported(const char* name) {
#ifdef __vita__
    printf("PASS %s\n", name);
    return 0;
#else
    CHECK(bw_vita_clocks_apply_max() == 1, "host apply reported success");
    printf("PASS %s\n", name);
    return 0;
#endif
}

typedef struct {
    const char* name;
    int (*run)(const char* name);
} Case;

static const Case CASES[] = {
    {"target-constants", case_target_constants},
    {"raises-below", case_raises_below},
    {"noop-at-target", case_noop_at_target},
    {"never-lowers", case_never_lowers},
    {"mixed-axes", case_mixed_axes},
    {"failure-isolated", case_failure_isolated},
    {"host-unsupported", case_host_is_unsupported},
};

int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    int failures = 0;
    bool known = false;
    for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; ++i) {
        if (strcmp(argv[1], "all") != 0 && strcmp(argv[1], CASES[i].name) != 0)
            continue;
        known = true;
        failures += CASES[i].run(CASES[i].name);
    }
    if (strcmp(argv[1], "all") != 0 && !known)
        return 2;
    return failures == 0 ? 0 : 1;
}
