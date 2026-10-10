#include "gx_checkpoint.h"

static bool frame(WwGxCheckpoint* checkpoint) {
    switch (checkpoint->bytes[0]) {
    case 0x00u:
    case DOL_GX_CMD_INVL_VC:
        checkpoint->required = 1u;
        break;
    case DOL_GX_CMD_LOAD_CP_REG:
        checkpoint->required = 6u;
        break;
    case DOL_GX_CMD_LOAD_BP_REG:
    case 0x20u:
    case 0x28u:
    case 0x30u:
    case 0x38u:
        checkpoint->required = 5u;
        break;
    case DOL_GX_CMD_CALL_DL:
        checkpoint->required = 9u;
        break;
    case DOL_GX_CMD_LOAD_XF_REG:
        if (checkpoint->size >= 5u) {
            const u32 count = ((read_be32(checkpoint->bytes + 1u) >> 16) & 0xFFFFu) + 1u;
            if (count > 255u) {
                checkpoint->error = "XF count exceeds pinned parser's u8 capacity";
                return false;
            }
            checkpoint->required = 5u + count * 4u;
        }
        break;
    default:
        checkpoint->error = "unsupported first GX opcode (draw requires preceding layout)";
        return false;
    }
    return true;
}

bool ww_gx_write(WwGxCheckpoint* checkpoint, DolGxRecompState* gx,
                 u64 value, u8 size, u32 pc, u32 sp) {
    if (checkpoint == NULL || gx == NULL)
        return false;
    if (checkpoint->error != NULL)
        return false;
    if (size != 1u && size != 2u && size != 4u && size != 8u) {
        checkpoint->error = "invalid FIFO write width";
        return false;
    }
    for (u8 index = 0u; index < size; ++index) {
        if (checkpoint->complete) {
            checkpoint->trailing_bytes++;
            continue;
        }
        if (checkpoint->size == 0u) {
            checkpoint->pc = pc;
            checkpoint->sp = sp;
        }
        if (checkpoint->size == sizeof checkpoint->bytes) {
            checkpoint->error = "GX frame exceeds bounded command buffer";
            return false;
        }
        checkpoint->bytes[checkpoint->size++] = (u8)(value >> ((size - index - 1u) * 8u));
        if (!frame(checkpoint))
            return false;
        if (checkpoint->required != 0u && checkpoint->size == checkpoint->required) {
            if (!dol_gx_recomp_replay_fifo(gx, checkpoint->bytes, checkpoint->size)) {
                checkpoint->error = "complete GX command rejected by pinned parser";
                return false;
            }
            checkpoint->completion_pc = pc;
            checkpoint->complete = true;
        }
    }
    return true;
}
