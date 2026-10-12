#include "boot_host.h"

static bool retained_flush_nops(const WwHost* host) {
    const WwGatherPipe* pipe = &host->gather;
    if (pipe->size >= sizeof(pipe->bytes))
        return false;
    for (u32 index = 0u; index < pipe->size; ++index) {
        const u32 pc = 0x803229C4u - ((pipe->size - 1u - index) / 4u) * 4u;
        const WwFifoOrigin* origin = &pipe->origins[index];
        if (pipe->bytes[index] != 0u || origin->byte != 0u || origin->pc != pc ||
            origin->sp != host->cpu.gpr[1] - 8u)
            return false;
    }
    return true;
}

static const char* trace_finish_frontier(const WwHost* host, bool nops) {
    if (fprintf(stderr, "[gx-finish-frontier] decoded=%llu gather_bytes=%u sourced_flush_nops=%u cp_distance=%u capture=%u captured_read=%u pending_command=%u\n",
                (unsigned long long)host->stream.decoded, host->gather.size, nops,
                host->cp_fifo_distance, host->fifo_capture.count, host->fifo_capture.read_bytes,
                host->stream.command.size) < 0)
        return "GX finish frontier trace write failed";
    for (u32 index = 0u; index < host->gather.size && index < sizeof(host->gather.bytes); ++index) {
        const WwFifoOrigin* origin = &host->gather.origins[index];
        if (fprintf(stderr, "[gx-finish-gather] index=%u byte=%02X origin_byte=%02X pc=%08X sp=%08X\n",
                    index, host->gather.bytes[index], origin->byte, origin->pc, origin->sp) < 0)
            return "GX finish gather trace write failed";
    }
    return NULL;
}

const char* ww_host_finish_gpu(WwHost* host) {
    if (host == NULL)
        return "GX draw-done requires a host";
    if (host->renderer == NULL || !host->stream.enabled)
        return "GX draw-done requires the actual renderer stream";
    const bool nops = retained_flush_nops(host);
    const bool consumed = nops && host->cp_fifo_distance == 0u && host->fifo_capture.count == 0u &&
        host->fifo_capture.read_bytes == 0u && host->stream.command.size == 0u;
    if (consumed && host->gpu_finish_committed && host->gpu_finish_index == host->stream.decoded)
        return NULL;
    const char* error = NULL;
    if (!host->gpu_finish_committed || !consumed) {
        error = trace_finish_frontier(host, nops);
        if (error != NULL)
            return error;
    }
    if (!consumed)
        return "GX draw-done requires all captured FIFO work to be consumed";
    error = ww_renderer_finish(host->renderer);
    if (error != NULL)
        return error;
    if (fprintf(stderr, "[gx-gpu-finish] decoded=%llu pc=%08X lr=%08X queue=%08X backend=confirmed-finish\n",
                (unsigned long long)host->stream.decoded, host->cpu.pc, host->cpu.lr, host->cpu.gpr[3]) < 0)
        return "GX GPU finish trace write failed";
    dol_interrupts_commit_pe_finish(&host->interrupts);
    host->gpu_finish_index = host->stream.decoded;
    host->gpu_finish_committed = true;
    return NULL;
}
