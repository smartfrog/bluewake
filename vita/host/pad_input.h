#ifndef WW05_PAD_INPUT_H
#define WW05_PAD_INPUT_H

#include "pad_wire.h"

typedef struct WwPadSample {
    u32 buttons;
    u8 lx, ly, rx, ry;
} WwPadSample;

// Return one fresh sample, zero for no sample, or the original negative SDK error.
typedef int (*WwPadRead)(void* user, WwPadSample* sample);

DolPadState ww_pad_map(const WwPadSample* sample);

struct WwHost;
const char* ww_pad_si_write(struct WwHost* host, u32 address, u32 value);
#ifdef __vita__
int ww_pad_vita_enable(struct WwHost* host);
#endif

#endif
