#include "boot_host.h"

const char* ww_gx_stream_write(WwHost* host, u8 byte, const WwFifoOrigin* origin, u32 trailing) {
    WwGxStream* stream = &host->stream;
    if (stream->log_limit == 0u || stream->log_limit > WW_GX_MAX_JOURNAL_COMMANDS)
        return "invalid GX command journal limit";
    if (stream->decoded == UINT64_MAX)
        return "GX decoded command counter exhausted";
    if (host->gx.trace_count >= DOL_GX_RECOMP_MAX_TRACE_EVENTS)
        return "pinned GX trace is full before command consumption";
    WwGxCheckpoint* command = &stream->command;
    if (!ww_gx_write(command, &host->gx, byte, 1u, origin->pc, origin->sp))
        return command->error;
    if (!stream->first_draw_observed && command->size == 3u && command->bytes[0] >= 0x80u) {
        const char* error = ww_gx_observe_first_draw(host);
        if (error != NULL)
            return error;
    }
    if (!command->complete)
        return NULL;
    if (host->gx.trace_count >= DOL_GX_RECOMP_MAX_TRACE_EVENTS)
        return "pinned GX command trace reached capacity before renderer acceptance";
    const u64 index = stream->decoded + 1u;
    if (stream->logged < stream->log_limit) {
        if (stream->logged_bytes > WW_GX_MAX_JOURNAL_BYTES ||
            command->size > WW_GX_MAX_JOURNAL_BYTES - stream->logged_bytes)
            return "GX command journal exhausted its byte budget";
        if (command->bytes[0] >= 0x80u &&
            fprintf(stderr, "[gx-command-layout] index=%llu format=%u vertices=%u stride=%u bytes=%u\n",
                    (unsigned long long)index, command->bytes[0] & 7u,
                    read_be16(command->bytes + 1u), host->gx.vertex_layouts[command->bytes[0] & 7u].vertex_size,
                    command->size) < 0)
            return "GX command layout journal write failed";
        if (fprintf(stderr, "[gx-command] index=%llu opcode=%02X bytes=%u pc=%08X completion_pc=%08X sp=%08X hex=",
                    (unsigned long long)index, command->bytes[0], command->size,
                    command->pc, command->completion_pc, command->sp) < 0)
            return "GX command journal write failed";
        for (u32 offset = 0u; offset < command->size; ++offset) {
            if (fprintf(stderr, "%02X", command->bytes[offset]) < 0)
                return "GX command journal byte write failed";
        }
        if (fprintf(stderr, "\n") < 0)
            return "GX command journal closure failed";
        stream->logged++;
        stream->logged_bytes += command->size;
    }
    if (!host->frontier.complete) {
        host->frontier = *command;
        host->frontier.trailing_bytes = trailing;
    }
    stream->decoded = index;
    ww_gx_remember_command(stream, command);
    if (command->bytes[0] >= 0x80u && index <= stream->log_limit) {
        const char* capture_error = ww_host_capture_mem1(host);
        if (capture_error != NULL)
            return capture_error;
    }
    // VITA-WW-17: the same captured bytes the pinned trace observes go to the
    // renderer, in stream order. The present call shows a frame the game
    // finished (a display copy) and does nothing otherwise; when the march's
    // VI/present path lands, move the present call there. A renderer failure is
    // the host's failure: return it and stop.
    if (host->renderer != NULL) {
        const char* renderer_error = ww_renderer_write(host->renderer, command->bytes, command->size);
        if (renderer_error == NULL) {
            bool presented = false;
            renderer_error = ww_renderer_present(host->renderer, &presented);
            if (presented) {
                if (renderer_error != NULL &&
                    fprintf(stderr, "[renderer-after-present] error=%s\n", renderer_error) < 0)
                    return renderer_error;
                WwRendererStats stats;
                ww_renderer_stats(host->renderer, &stats);
                const char* marker_error = ww_host_frame_present(host, stats.presented);
                if (marker_error != NULL)
                    return marker_error;
            }
        }
        if (renderer_error != NULL) {
            *command = (WwGxCheckpoint){0};
            return renderer_error;
        }
        host->gx.fifo.size = 0u;
        host->gx.trace_count = 0u;
    }
    *command = (WwGxCheckpoint){0};
    if (host->mem1_capture.end_command != 0u && index == host->mem1_capture.end_command)
        return "scene MEM1 draw window capture complete";
    return NULL;
}
