#ifndef BLUEWAKE_DISPATCH_TRACE_H
#define BLUEWAKE_DISPATCH_TRACE_H

#include "core/cpu.h"

#include <stdint.h>

#define BW_TRACE_RING 8u

typedef struct BwCallTrace {
    uintptr_t ctx;
    uintptr_t host_call;
    u32 address;
    u32 pc;
    u32 lr;
    u32 srr0;
    u32 exception;
} BwCallTrace;

typedef struct BwTailTrace {
    uintptr_t host;
    uintptr_t module;
    uintptr_t dispatch;
} BwTailTrace;

void bw_trace_call(CPUState* ctx, u32 address);
void bw_trace_dispatch(const void* host, const void* module, const void* dispatch);
void bw_trace_dump(const char* why);
unsigned bw_trace_call_snapshot(BwCallTrace* out, unsigned capacity);
unsigned bw_trace_tail_snapshot(BwTailTrace* out, unsigned capacity);

#endif
