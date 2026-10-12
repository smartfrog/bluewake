#include "boot_host.h"
#include <string.h>

const char* ww_fifo_consume_first(WwHost* host) {
    if (host == NULL)
        return "CP reader requires a host";
    if (host->error != NULL)
        return host->error;
    if (host->frontier.error != NULL)
        return host->frontier.error;
    if ((host->cp_control & 1u) == 0u || (host->frontier.complete && !host->stream.enabled))
        return NULL;
    if ((host->cp_control & 0x10u) == 0u)
        return "CP reader requires the implemented linked FIFO";
    if ((dol_interrupts_pi_cause(&host->interrupts) & DOL_PI_CAUSE_CP) != 0u)
        return "CP reader requires pending CP IRQ service not implemented";

    WwFifoCapture* capture = &host->fifo_capture;
    if (host->frontier.size != 0u && capture->read_bytes == 0u &&
        (!host->stream.enabled || host->stream.decoded == 0u))
        return "CP reader cannot continue an unsourced checkpoint prefix";
    while (host->cp_fifo_distance != 0u && (host->stream.enabled || !host->frontier.complete)) {
        CPUState* cpu = &host->cpu;
        const u32 physical = host->cp_fifo_read_pointer;
        if (!ww_fifo_mem1_ring_valid(cpu, host->cp_fifo_base, host->cp_fifo_end, physical))
            return "CP read requires an aligned physical ring in real MEM1";
        const u32 span = host->cp_fifo_end - host->cp_fifo_base + 32u;
        if ((host->cp_fifo_distance & 31u) != 0u || host->cp_fifo_distance > span - 32u)
            return "CP read distance is not a valid produced block count";
        const u32 expected_write = host->cp_fifo_base +
            (physical - host->cp_fifo_base + host->cp_fifo_distance) % span;
        if (host->cp_fifo_write_pointer != expected_write)
            return "CP read distance and write pointer are inconsistent";
        if ((host->cp_control & 4u) != 0u && host->cp_fifo_distance > host->cp_fifo_high_watermark)
            return "CP reader requires overflow IRQ service not implemented";
        if (capture->count > WW_FIFO_ORIGIN_BYTES || capture->read_bytes > capture->count ||
            capture->count - capture->read_bytes < 32u || (capture->read_bytes & 31u) != 0u ||
            capture->first_pointer < host->cp_fifo_base || capture->first_pointer > host->cp_fifo_end)
            return "CP read requires retained first-command writer provenance";
        const u32 expected_read = host->cp_fifo_base +
            (capture->first_pointer - host->cp_fifo_base + capture->read_bytes) % span;
        if (physical != expected_read)
            return "CP read pointer no longer matches the retained writer provenance";
        const u32 guest = GC_RAM_BASE | physical;
        if (ww_host_linked_alias_overlaps(guest, 32u) || get_ram_ptr(cpu, guest, 32u, NULL) != cpu->ram + physical)
            return "CP read cannot consume a linked alias";
        if (capture->transfers == UINT64_MAX)
            return "CP read transfer counter exhausted";
        if (capture->total_read_bytes > UINT64_MAX - 32u)
            return "CP read byte counter exhausted";

        u8 bytes[32];
        memcpy(bytes, cpu->ram + physical, sizeof(bytes));
        const WwFifoOrigin* origins = capture->origins + capture->read_bytes;
        for (u32 index = 0u; index < sizeof(bytes); ++index) {
            if (bytes[index] != origins[index].byte)
                return "CP FIFO bytes changed after their sourced gather writes";
        }
        const u32 next = physical == host->cp_fifo_end ? host->cp_fifo_base : physical + 32u;
        const u32 distance = host->cp_fifo_distance - 32u;
        const u64 transfer = capture->transfers + 1u;
        if (ww_host_trace_transfer(transfer) &&
            fprintf(stderr, "[cp-consume] transfer=%llu main=%08X bytes=32 rptr=%08X next_rptr=%08X wptr=%08X distance_before=%u distance_after=%u pc=%08X\n",
                    (unsigned long long)transfer, guest, physical, next, host->cp_fifo_write_pointer,
                    host->cp_fifo_distance, distance, cpu->pc) < 0)
            return "CP consume trace write failed";

        host->cp_fifo_read_pointer = next;
        host->cp_fifo_distance = distance;
        capture->read_bytes += 32u;
        capture->transfers = transfer;
        capture->total_read_bytes += 32u;
        for (u32 index = 0u; index < sizeof(bytes); ++index) {
            if (host->stream.enabled) {
                const char* error = ww_gx_stream_write(host, bytes[index], origins + index, 31u - index);
                if (error != NULL)
                    return error;
            } else if (!ww_gx_write(&host->frontier, &host->gx, bytes[index], 1u, origins[index].pc, origins[index].sp))
                return host->frontier.error;
        }
        if (host->stream.enabled) {
            capture->count -= 32u;
            memmove(capture->origins, capture->origins + 32u, capture->count * sizeof(capture->origins[0]));
            capture->read_bytes = 0u;
            capture->first_pointer = next;
        }
    }
    return NULL;
}
