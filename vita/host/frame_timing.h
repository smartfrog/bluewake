#ifndef WW05_FRAME_TIMING_H
#define WW05_FRAME_TIMING_H

#include "core/types.h"

typedef struct WwFrameStamp {
    u64 index;
    u64 us;
} WwFrameStamp;

typedef struct WwFrameTiming {
    WwFrameStamp retrace;
    WwFrameStamp present;
    u64 work_us;
    u64 work_cycles;
    u64 work_turns;
    u64 work_leap_cycles;
    u64 work_leap_wall_us;
    u64 work_present_us;
    u64 work_finish_us;
    u64 work_gpu_finish_calls;
    u64 work_draws;
    u64 work_irq500;
    u64 work_irq900;
    u64 present_turns;
} WwFrameTiming;

const char* ww_host_wall_us(u64* us);

#endif
