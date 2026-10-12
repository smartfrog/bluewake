#include "boot_host.h"

const char* ww_host_finish_gpu(WwHost* host) {
    if (host == NULL)
        return "GX draw-done requires a host";
    if (host->renderer == NULL || !host->stream.enabled)
        return "GX draw-done requires the actual renderer stream";
    if (host->gather.size != 0u || host->cp_fifo_distance != 0u || host->fifo_capture.count != 0u ||
        host->fifo_capture.read_bytes != 0u || host->stream.command.size != 0u)
        return "GX draw-done requires all captured FIFO work to be consumed";
    if (host->gpu_finish_committed && host->gpu_finish_index == host->stream.decoded)
        return NULL;
    const char* error = ww_renderer_finish(host->renderer);
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
