#include "gx_checkpoint.h"

static bool frame(WwGxCheckpoint* checkpoint, DolGxRecompState* gx) {
    if (checkpoint->required != 0u)
        return true;
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
    default: {
        switch (checkpoint->bytes[0] & 0xF8u) {
        case 0x80u:
        case 0x90u:
        case 0x98u:
        case 0xA0u:
        case 0xA8u:
        case 0xB0u:
        case 0xB8u:
            break;
        default:
            checkpoint->error = "unsupported GX opcode";
            return false;
        }
        if (checkpoint->size < 3u)
            break;
        const u32 count = read_be16(checkpoint->bytes + 1u);
        const u8 format = checkpoint->bytes[0] & 7u;
        if (count == 0u) {
            checkpoint->error = "zero-vertex draw is unsupported by the pinned checkpoint parser";
            return false;
        }
        if (!gx->vertex_layouts[format].valid && !dol_gx_recomp_derive_vertex_layout(gx, format)) {
            checkpoint->error = "GX draw requires a real preceding CP vertex layout";
            return false;
        }
        const u64 required = 3u + (u64)count * gx->vertex_layouts[format].vertex_size;
        if (required > sizeof(checkpoint->bytes)) {
            checkpoint->error = "GX draw exceeds bounded command buffer";
            return false;
        }
        checkpoint->required = (u32)required;
        break;
    }
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
        if (!frame(checkpoint, gx))
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
