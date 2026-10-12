#ifndef WW05_FIFO_GATHER_H
#define WW05_FIFO_GATHER_H

#include "core/cpu.h"

typedef struct WwGatherPipe {
    u8 bytes[32];
    u8 size;
    u64 transfers;
} WwGatherPipe;

struct WwHost;
const char* ww_fifo_gather_write(struct WwHost* host, u64 value, u8 size);

#endif
