#include "dispatch_trace.h"

#include <stdio.h>

static BwCallTrace g_calls[BW_TRACE_RING];
static unsigned g_call_count;
static BwTailTrace g_tails[BW_TRACE_RING];
static unsigned g_tail_count;
static int g_dumped;

static unsigned ring_start(unsigned count) {
    return count > BW_TRACE_RING ? count - BW_TRACE_RING : 0u;
}

void bw_trace_call(CPUState* ctx, u32 address) {
    BwCallTrace* entry = &g_calls[g_call_count % BW_TRACE_RING];
    entry->ctx = (uintptr_t)ctx;
    entry->host_call = ctx != NULL ? (uintptr_t)ctx->host_call : 0u;
    entry->address = address;
    entry->pc = ctx != NULL ? ctx->pc : 0u;
    entry->lr = ctx != NULL ? ctx->lr : 0u;
    entry->srr0 = ctx != NULL ? ctx->srr0 : 0u;
    entry->exception = ctx != NULL ? ctx->exception : 0u;
    g_call_count++;
    if (entry->host_call == 8u) {
        fprintf(stderr, "[dispatch-trace] host_call=8 address=%08X pc=%08X lr=%08X srr0=%08X exception=%08X\n",
                entry->address, entry->pc, entry->lr, entry->srr0, entry->exception);
    }
}

void bw_trace_dispatch(const void* host, const void* module, const void* dispatch) {
    BwTailTrace* entry = &g_tails[g_tail_count % BW_TRACE_RING];
    entry->host = (uintptr_t)host;
    entry->module = (uintptr_t)module;
    entry->dispatch = (uintptr_t)dispatch;
    g_tail_count++;
    if (entry->dispatch == 8u) {
        fprintf(stderr, "[dispatch-trace] dispatch=8 host=%p module=%p\n", host, module);
    }
}

void bw_trace_dump(const char* why) {
    if (g_dumped)
        return;
    g_dumped = 1;
    fprintf(stderr, "[dispatch-trace] why=%s calls=%u tails=%u\n",
            why != NULL ? why : "?", g_call_count, g_tail_count);
    const unsigned call_start = ring_start(g_call_count);
    for (unsigned i = call_start; i < g_call_count; ++i) {
        const BwCallTrace* entry = &g_calls[i % BW_TRACE_RING];
        fprintf(stderr, "[dispatch-trace] call[%u] ctx=%p host_call=%p address=%08X pc=%08X lr=%08X srr0=%08X exception=%08X\n",
                i, (void*)entry->ctx, (void*)entry->host_call, entry->address,
                entry->pc, entry->lr, entry->srr0, entry->exception);
    }
    const unsigned tail_start = ring_start(g_tail_count);
    for (unsigned i = tail_start; i < g_tail_count; ++i) {
        const BwTailTrace* entry = &g_tails[i % BW_TRACE_RING];
        fprintf(stderr, "[dispatch-trace] tail[%u] host=%p module=%p dispatch=%p\n",
                i, (void*)entry->host, (void*)entry->module, (void*)entry->dispatch);
    }
}

unsigned bw_trace_call_snapshot(BwCallTrace* out, unsigned capacity) {
    if (out == NULL || capacity == 0u)
        return 0u;
    const unsigned start = ring_start(g_call_count);
    unsigned written = 0u;
    for (unsigned i = start; i < g_call_count && written < capacity; ++i, ++written)
        out[written] = g_calls[i % BW_TRACE_RING];
    return written;
}

unsigned bw_trace_tail_snapshot(BwTailTrace* out, unsigned capacity) {
    if (out == NULL || capacity == 0u)
        return 0u;
    const unsigned start = ring_start(g_tail_count);
    unsigned written = 0u;
    for (unsigned i = start; i < g_tail_count && written < capacity; ++i, ++written)
        out[written] = g_tails[i % BW_TRACE_RING];
    return written;
}
