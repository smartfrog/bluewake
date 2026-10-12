#include "boot_host.h"
#include "gxruntime/guest_memory_dirty.h"
#include <string.h>

const char* ww_fifo_gather_write(WwHost* host, u64 value, u8 size) {
    if (host == NULL)
        return "gather write requires a host";
    if (size != 1u && size != 2u && size != 4u && size != 8u)
        return "invalid gather write width";
    WwGatherPipe* pipe = &host->gather;
    if (pipe->size >= sizeof(pipe->bytes))
        return "invalid retained gather byte count";

    u8 bytes[sizeof(pipe->bytes) + 8u];
    WwFifoOrigin origins[sizeof(pipe->bytes) + 8u] = {0};
    memcpy(bytes, pipe->bytes, pipe->size);
    memcpy(origins, pipe->origins, pipe->size * sizeof(origins[0]));
    for (u8 index = 0u; index < size; ++index) {
        bytes[pipe->size + index] = (u8)(value >> ((size - index - 1u) * 8u));
        origins[pipe->size + index].pc = host->cpu.pc;
        origins[pipe->size + index].sp = host->cpu.gpr[1];
        origins[pipe->size + index].byte = bytes[pipe->size + index];
    }
    const u8 count = pipe->size + size;
    if (count < sizeof(pipe->bytes)) {
        memcpy(pipe->bytes, bytes, count);
        memcpy(pipe->origins, origins, count * sizeof(origins[0]));
        pipe->size = count;
        return NULL;
    }

    CPUState* cpu = &host->cpu;
    const u32 physical = host->pi_fifo_write_pointer;
    if (!ww_fifo_mem1_ring_valid(cpu, host->pi_fifo_base, host->pi_fifo_end, physical))
        return "gather burst requires an aligned physical PI ring in real MEM1";
    const u32 guest = GC_RAM_BASE | physical;
    if (ww_host_linked_alias_overlaps(guest, 32u) || get_ram_ptr(cpu, guest, 32u, NULL) != cpu->ram + physical)
        return "gather burst cannot copy into a linked alias";
    const bool linked = (host->cp_control & 0x10u) != 0u;
    WwFifoCapture* capture = &host->fifo_capture;
    if (linked) {
        if (host->cp_fifo_base != host->pi_fifo_base || host->cp_fifo_end != host->pi_fifo_end ||
            host->cp_fifo_write_pointer != physical)
            return "linked gather burst requires matching PI and CP rings and write pointers";
        const u32 capacity = host->cp_fifo_end - host->cp_fifo_base;
        if ((host->cp_fifo_distance & 31u) != 0u || capacity < 32u || host->cp_fifo_distance > capacity - 32u)
            return "linked gather burst exceeds unconsumed CP ring capacity";
        if (capture->count > WW_FIFO_ORIGIN_BYTES || (capture->count & 31u) != 0u)
            return "invalid retained FIFO origin count";
        if (capture->count == 0u && (capture->read_bytes != 0u || host->cp_fifo_distance != 0u ||
                                    host->cp_fifo_read_pointer != physical))
            return "first linked gather burst requires the observed empty CP read position";
    }
    if (pipe->transfers == UINT64_MAX)
        return "gather burst counter exhausted";
    const u64 transfer = pipe->transfers + 1u;
    const u32 next = physical == host->pi_fifo_end ? host->pi_fifo_base : physical + 32u;
    const u32 distance = host->cp_fifo_distance + (linked ? 32u : 0u);
    if (ww_host_trace_transfer(transfer) &&
        fprintf(stderr, "[gather-burst] transfer=%llu main=%08X bytes=32 linked=%u distance=%u pc=%08X pi_wptr=%08X cp_wptr=%08X rptr=%08X\n",
                (unsigned long long)transfer, guest, linked, distance, cpu->pc, next,
                linked ? next : host->cp_fifo_write_pointer, host->cp_fifo_read_pointer) < 0)
        return "gather burst trace write failed";

    memcpy(cpu->ram + physical, bytes, 32u);
    dol_guest_memory_dirty_mark(guest, 32u);
    clear_matching_reservation(cpu, guest);
    clear_matching_reservation(cpu, physical);
    host->pi_fifo_write_pointer = next;
    if (linked) {
        if (capture->count == 0u)
            capture->first_pointer = physical;
        if (capture->count < WW_FIFO_ORIGIN_BYTES) {
            memcpy(capture->origins + capture->count, origins, 32u * sizeof(origins[0]));
            capture->count += 32u;
        }
        host->cp_fifo_write_pointer = next;
        host->cp_fifo_distance = distance;
    }
    pipe->size = count - 32u;
    memcpy(pipe->bytes, bytes + 32u, pipe->size);
    memcpy(pipe->origins, origins + 32u, pipe->size * sizeof(origins[0]));
    pipe->transfers = transfer;
    return NULL;
}
