#ifndef WW05_GX_CHECKPOINT_H
#define WW05_GX_CHECKPOINT_H

#include "gxruntime/gx_recomp.h"

// The staging buffer holds one complete GX command before it is replayed. The
// largest XF load is 5 + 255*4 = 1025 bytes, which is why the historical bound
// fits it exactly. Inline draws are larger: the witnessed title draw at pc
// 0x8015D0EC emits 3 + 65*20 = 1303 bytes, so 1025 fails closed on it. 8192
// covers about six times that draw. It is a deliberate trial budget, not a
// derived maximum: the parser's worst case is far larger (count up to 65535 and
// stride up to roughly 96 from CP VAT/vcd), so a bigger draw still fails loud in
// gx_checkpoint.c and gx_stream.c rather than overflowing. 1025 is the default;
// a trial raises it (WW05_GX_MAX_COMMAND_BYTES).
#ifndef WW_GX_MAX_COMMAND_BYTES
#define WW_GX_MAX_COMMAND_BYTES 1025u
#endif
enum { WW_GX_MAX_JOURNAL_COMMANDS = 196608u, WW_GX_MAX_JOURNAL_BYTES = 8u * 1024u * 1024u };

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
    u32 logged_bytes;
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
