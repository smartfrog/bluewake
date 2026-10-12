#ifndef WW05_FIFO_GATHER_H
#define WW05_FIFO_GATHER_H

#include "gx_checkpoint.h"
#include "core/cpu.h"

// Preserve the bounded first command's provenance through complete 32-byte read blocks.
enum { WW_FIFO_ORIGIN_BYTES = (WW_GX_MAX_COMMAND_BYTES + 31u) & ~31u };

typedef struct WwFifoOrigin {
    u32 pc;
    u32 sp;
    u8 byte;
} WwFifoOrigin;

typedef struct WwGatherPipe {
    u8 bytes[32];
    WwFifoOrigin origins[32];
    u8 size;
    u64 transfers;
} WwGatherPipe;

typedef struct WwFifoCapture {
    WwFifoOrigin origins[WW_FIFO_ORIGIN_BYTES];
    u32 count;
    u32 read_bytes;
    u32 first_pointer;
    u64 transfers;
    u64 total_read_bytes;
} WwFifoCapture;

struct WwHost;
const char* ww_fifo_gather_write(struct WwHost* host, u64 value, u8 size);
const char* ww_fifo_consume_first(struct WwHost* host);

static inline bool ww_fifo_mem1_ring_valid(const CPUState* cpu, u32 base, u32 end, u32 pointer) {
    return cpu != NULL && cpu->ram != NULL && cpu->ram_size >= 32u && base <= end &&
           end <= cpu->ram_size - 32u && pointer >= base && pointer <= end &&
           ((base | end | pointer) & 31u) == 0u;
}

#endif
