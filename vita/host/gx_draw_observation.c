#include "boot_host.h"
#include <string.h>

void ww_gx_remember_command(WwGxStream* stream, const WwGxCheckpoint* command) {
    if (stream->first_draw_observed)
        return;
    stream->recent[stream->recent_next] = *command;
    stream->recent_next = (stream->recent_next + 1u) % WW_GX_RECENT_COMMANDS;
    if (stream->recent_count < WW_GX_RECENT_COMMANDS)
        ++stream->recent_count;
    if (command->bytes[0] != DOL_GX_CMD_LOAD_CP_REG)
        return;
    WwGxCpWrite* cp = &stream->cp_writes[stream->cp_next];
    memcpy(cp->bytes, command->bytes, sizeof(cp->bytes));
    cp->index = stream->decoded;
    cp->pc = command->pc;
    cp->completion_pc = command->completion_pc;
    cp->sp = command->sp;
    stream->cp_next = (stream->cp_next + 1u) % WW_GX_RECENT_CP_WRITES;
    if (stream->cp_count < WW_GX_RECENT_CP_WRITES)
        ++stream->cp_count;
}

const char* ww_gx_observe_first_draw(WwHost* host) {
    WwGxStream* stream = &host->stream;
    const WwGxCheckpoint* draw = &stream->command;
    const u8 format = draw->bytes[0] & 7u;
    const DolGxRecompState* gx = &host->gx;
    if (fprintf(stderr, "[gx-draw-layout] index=%llu opcode=%02X vertices=%u format=%u stride=%u bytes=%u vcd_lo=%08X vcd_hi=%08X vcd_valid=%u,%u vat=%08X,%08X,%08X vat_valid=%u,%u,%u recent=%u cp_writes=%u pc=%08X sp=%08X\n",
                (unsigned long long)(stream->decoded + 1u), draw->bytes[0], read_be16(draw->bytes + 1u), format,
                gx->vertex_layouts[format].vertex_size, draw->required, gx->vcd_lo, gx->vcd_hi,
                gx->vcd_lo_valid, gx->vcd_hi_valid, gx->vat_group0[format], gx->vat_group1[format], gx->vat_group2[format],
                gx->vat_group0_valid[format], gx->vat_group1_valid[format], gx->vat_group2_valid[format],
                stream->recent_count, stream->cp_count, draw->pc, draw->sp) < 0)
        return "first GX draw layout trace write failed";
    const u32 oldest = (stream->recent_next + WW_GX_RECENT_COMMANDS - stream->recent_count) % WW_GX_RECENT_COMMANDS;
    for (u32 index = 0u; index < stream->recent_count; ++index) {
        const WwGxCheckpoint* command = &stream->recent[(oldest + index) % WW_GX_RECENT_COMMANDS];
        const u64 ordinal = stream->decoded - stream->recent_count + index + 1u;
        if (fprintf(stderr, "[gx-before-draw] index=%llu opcode=%02X bytes=%u pc=%08X completion_pc=%08X sp=%08X hex=",
                    (unsigned long long)ordinal, command->bytes[0], command->size, command->pc,
                    command->completion_pc, command->sp) < 0)
            return "first GX draw command history trace write failed";
        for (u32 byte = 0u; byte < command->size; ++byte)
            if (fprintf(stderr, "%02X", command->bytes[byte]) < 0)
                return "first GX draw history byte trace write failed";
        if (fprintf(stderr, "\n") < 0)
            return "first GX draw history closure failed";
    }
    const u32 cp_oldest = (stream->cp_next + WW_GX_RECENT_CP_WRITES - stream->cp_count) % WW_GX_RECENT_CP_WRITES;
    for (u32 index = 0u; index < stream->cp_count; ++index) {
        const WwGxCpWrite* cp = &stream->cp_writes[(cp_oldest + index) % WW_GX_RECENT_CP_WRITES];
        if (fprintf(stderr, "[gx-cp-before-draw] index=%llu register=%02X value=%08X pc=%08X completion_pc=%08X sp=%08X\n",
                    (unsigned long long)cp->index, cp->bytes[1], read_be32(cp->bytes + 2u), cp->pc,
                    cp->completion_pc, cp->sp) < 0)
            return "first GX draw CP history trace write failed";
    }
    stream->first_draw_observed = true;
    return NULL;
}
