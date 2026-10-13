#ifndef WW05_MEM1_CAPTURE_H
#define WW05_MEM1_CAPTURE_H

#include "core/types.h"

enum { WW_MEM1_CAPTURE_BYTES = 24u * 1024u * 1024u, WW_MEM1_MAX_CAPTURES = 2u };

typedef struct WwMem1Capture {
    u32 limit;
    u32 captured;
    u64 last_us;
    const char* token;
    const char* source;
    const char* package;
} WwMem1Capture;

#endif
