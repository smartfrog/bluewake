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
} WwFrameTiming;

#endif
