#ifndef BLUEWAKE_PAD_WIRE_H
#define BLUEWAKE_PAD_WIRE_H

#include "gxruntime/headless_backend.h"

typedef enum BluewakePadReply {
    BLUEWAKE_PAD_REPLY_UNSUPPORTED,
    BLUEWAKE_PAD_REPLY_ID,
    BLUEWAKE_PAD_REPLY_ORIGIN,
    BLUEWAKE_PAD_REPLY_LIVE
} BluewakePadReply;

BluewakePadReply bluewake_pad_wire_reply_kind(u32 command, u32 input_length);
bool bluewake_pad_wire_reply(BluewakePadReply kind, const DolPadState* pad, u16 buttons,
                             u32* data0_out, u32* data1_out);
u32 bluewake_pad_wire_poll_mask(u32 poll);

void bluewake_pad_wire_encode(const DolPadState* pad, u16 buttons,
                              u32* data0_out, u32* data1_out);
void bluewake_pad_merge(const DolPadState* configured,
                        const DolPadState* live, DolPadState* merged_out);

#endif
