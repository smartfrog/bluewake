#ifndef WW05_FRAME_TIMING_H
#define WW05_FRAME_TIMING_H

#include "core/types.h"

typedef struct WwFrameStamp {
    u64 index;
    u64 us;
} WwFrameStamp;

typedef struct WwFrameCapture {
    u64 index;
    u64 us;
    u64 start;
    u64 limit;
} WwFrameCapture;

typedef struct WwFrameTiming {
    WwFrameStamp retrace;
    WwFrameStamp present;
} WwFrameTiming;

const char* ww_host_wall_us(u64* us);

#endif
