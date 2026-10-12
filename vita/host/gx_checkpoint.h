#ifndef WW05_GX_CHECKPOINT_H
#define WW05_GX_CHECKPOINT_H

#include "gxruntime/gx_recomp.h"

enum { WW_GX_MAX_COMMAND_BYTES = 1025u };
enum { WW_GX_MAX_JOURNAL_COMMANDS = 2048u };

typedef struct WwGxCheckpoint {
    u8 bytes[WW_GX_MAX_COMMAND_BYTES];
    u32 size;
    u32 required;
    u32 pc;
    u32 sp;
    u32 completion_pc;
    u64 trailing_bytes;
    bool complete;
    const char* error;
} WwGxCheckpoint;

enum { WW_GX_RECENT_COMMANDS = 16u, WW_GX_RECENT_CP_WRITES = 64u };

typedef struct WwGxCpWrite {
    u8 bytes[6];
    u64 index;
    u32 pc;
    u32 completion_pc;
    u32 sp;
} WwGxCpWrite;

typedef struct WwGxStream {
    WwGxCheckpoint command;
    u64 decoded;
    u32 logged;
    u32 log_limit;
    bool enabled;
    WwGxCheckpoint recent[WW_GX_RECENT_COMMANDS];
    WwGxCpWrite cp_writes[WW_GX_RECENT_CP_WRITES];
    u32 recent_count;
    u32 recent_next;
    u32 cp_count;
    u32 cp_next;
    bool first_draw_observed;
} WwGxStream;

bool ww_gx_write(WwGxCheckpoint* checkpoint, DolGxRecompState* gx,
                 u64 value, u8 size, u32 pc, u32 sp);

#endif
