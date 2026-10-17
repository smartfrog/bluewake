// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BW_VITA_CLOCKS_H
#define BW_VITA_CLOCKS_H

// Boot-time clock lever ported from the DuskGXM reference analysis
// (build-f5/dusk-reference-analysis-5f3a9c21/REPORT.md, section 4 rank 1).
//
// Target values are the official Vita ceiling DuskGXM requests at init:
// ARM 444 MHz, bus 222 MHz, GPU 222 MHz, GPU crossbar 166 MHz. Only axes below
// the target are touched, so this never lowers a clock and is a no-op on a
// device already at or above every target. It is off by default
// (WW05_ENABLE_MAX_CLOCKS=OFF) until a hardware run shows a gain and acceptable
// battery/heat behaviour.
//
// This is a hardware-target lever only. Vita3K is not required to honor ScePower
// clock requests, so an emulator run cannot demonstrate a clock gain.

enum {
    BW_VITA_CLOCK_ARM = 0,
    BW_VITA_CLOCK_BUS = 1,
    BW_VITA_CLOCK_GPU = 2,
    BW_VITA_CLOCK_XBAR = 3,
    BW_VITA_CLOCK_AXES = 4,
};

typedef struct {
    int mhz[BW_VITA_CLOCK_AXES];
} BwVitaClocks;

typedef struct {
    int raised_mask;
    int failed_mask;
} BwVitaClockOutcome;

// Applies one axis. Returns 0 on success, negative on failure.
typedef int (*BwVitaClockSetAxis)(int axis, int mhz);

BwVitaClocks bw_vita_clocks_max(void);

// Raises only the axes whose target is above `current`, in ARM, BUS, GPU, XBAR
// order. A failing axis does not stop the remaining ones. Returns masks of the
// axes raised and the axes that failed.
BwVitaClockOutcome bw_vita_clocks_raise(const BwVitaClocks* current,
                                        const BwVitaClocks* target,
                                        BwVitaClockSetAxis set);

// Vita: reads the four clocks, raises them to the ceiling and logs the result.
// Returns 0 when every needed axis was applied (or none was needed) and -1 when
// any request failed. Failures must never fail the boot; callers log and go on.
// Non-Vita hosts: returns 1 without touching any clock.
int bw_vita_clocks_apply_max(void);

#endif
