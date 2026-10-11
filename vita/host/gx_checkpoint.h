#ifndef WW05_GX_CHECKPOINT_H
#define WW05_GX_CHECKPOINT_H

#include "gxruntime/gx_recomp.h"

typedef struct WwGxCheckpoint {
    u8 bytes[1025];
    u32 size;
    u32 required;
    u32 pc;
    u32 sp;
    u32 completion_pc;
    u64 trailing_bytes;
    bool complete;
    const char* error;
} WwGxCheckpoint;

bool ww_gx_write(WwGxCheckpoint* checkpoint, DolGxRecompState* gx,
                 u64 value, u8 size, u32 pc, u32 sp);

#endif
