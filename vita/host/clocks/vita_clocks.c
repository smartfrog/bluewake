// SPDX-License-Identifier: GPL-3.0-or-later
#include "vita_clocks.h"

#include <stdio.h>

#ifdef __vita__
#include <psp2/power.h>
#endif

#define BW_VITA_CLOCK_ALL_AXES ((1 << BW_VITA_CLOCK_AXES) - 1)

BwVitaClocks bw_vita_clocks_max(void) {
    BwVitaClocks target = {{444, 222, 222, 166}};
    return target;
}

BwVitaClockOutcome bw_vita_clocks_raise(const BwVitaClocks* current,
                                        const BwVitaClocks* target,
                                        BwVitaClockSetAxis set) {
    BwVitaClockOutcome outcome = {0, 0};
    if (current == NULL || target == NULL || set == NULL)
        return (BwVitaClockOutcome){0, BW_VITA_CLOCK_ALL_AXES};
    for (int axis = 0; axis < BW_VITA_CLOCK_AXES; ++axis) {
        if (target->mhz[axis] <= current->mhz[axis])
            continue;
        if (set(axis, target->mhz[axis]) == 0)
            outcome.raised_mask |= 1 << axis;
        else
            outcome.failed_mask |= 1 << axis;
    }
    return outcome;
}

#ifdef __vita__

static int vita_read_axis(int axis) {
    switch (axis) {
    case BW_VITA_CLOCK_ARM:
        return scePowerGetArmClockFrequency();
    case BW_VITA_CLOCK_BUS:
        return scePowerGetBusClockFrequency();
    case BW_VITA_CLOCK_GPU:
        return scePowerGetGpuClockFrequency();
    case BW_VITA_CLOCK_XBAR:
        return scePowerGetGpuXbarClockFrequency();
    default:
        return -1;
    }
}

static int vita_set_axis(int axis, int mhz) {
    switch (axis) {
    case BW_VITA_CLOCK_ARM:
        return scePowerSetArmClockFrequency(mhz);
    case BW_VITA_CLOCK_BUS:
        return scePowerSetBusClockFrequency(mhz);
    case BW_VITA_CLOCK_GPU:
        return scePowerSetGpuClockFrequency(mhz);
    case BW_VITA_CLOCK_XBAR:
        return scePowerSetGpuXbarClockFrequency(mhz);
    default:
        return -1;
    }
}

int bw_vita_clocks_apply_max(void) {
    BwVitaClocks current = {{0, 0, 0, 0}};
    for (int axis = 0; axis < BW_VITA_CLOCK_AXES; ++axis)
        current.mhz[axis] = vita_read_axis(axis);
    const BwVitaClocks target = bw_vita_clocks_max();
    const BwVitaClockOutcome outcome = bw_vita_clocks_raise(&current, &target, vita_set_axis);
    if (fprintf(stderr,
                "[vita-clocks] arm=%d bus=%d gpu=%d xbar=%d target_arm=%d target_bus=%d "
                "target_gpu=%d target_xbar=%d raised=%X failed=%X\n",
                current.mhz[BW_VITA_CLOCK_ARM], current.mhz[BW_VITA_CLOCK_BUS],
                current.mhz[BW_VITA_CLOCK_GPU], current.mhz[BW_VITA_CLOCK_XBAR],
                target.mhz[BW_VITA_CLOCK_ARM], target.mhz[BW_VITA_CLOCK_BUS],
                target.mhz[BW_VITA_CLOCK_GPU], target.mhz[BW_VITA_CLOCK_XBAR],
                outcome.raised_mask, outcome.failed_mask) < 0)
        return -1;
    return outcome.failed_mask == 0 ? 0 : -1;
}

#else

int bw_vita_clocks_apply_max(void) {
    return 1;
}

#endif
