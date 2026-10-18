#include "dispatch_trace.h"

#include <stdio.h>
#include <string.h>

static BwCallTrace g_calls[BW_TRACE_RING];
static unsigned g_call_count;
static BwTailTrace g_tails[BW_TRACE_RING];
static unsigned g_tail_count;
static const char* g_last_why;
static unsigned g_suspect_call_dumps;
static unsigned g_suspect_dispatch_dumps;

static unsigned ring_start(unsigned count) {
    return count > BW_TRACE_RING ? count - BW_TRACE_RING : 0u;
}

/* ARM and Thumb function pointers legitimately use bit 0 as the state bit. The
 * target after masking is 2-byte aligned, so only the small/fill-pattern
 * checks apply. Data pointers (module) keep the 4-byte alignment check. */
static int implausible_function(uintptr_t pointer) {
    if (pointer == 0u) return 0;
    if (pointer < 0x1000u) return 1;
    pointer &= ~(uintptr_t)1u;
    return pointer == 0xCCCCCCCCu;
}

static int implausible_data(uintptr_t pointer) {
    if (pointer == 0u) return 0;
    if (pointer < 0x1000u || pointer == 0xCCCCCCCCu) return 1;
    return (pointer & 3u) != 0u;
}

static int guest_pointer_unusable(u32 pointer) {
    return pointer == 1u || pointer == 8u || pointer == 0xCCCCCCCCu ||
           (pointer != 0u && pointer < 0x80000000u);
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
    if (implausible_function(entry->host_call) || guest_pointer_unusable(entry->pc) ||
        guest_pointer_unusable(entry->lr) || guest_pointer_unusable(entry->srr0)) {
        fprintf(stderr, "[dispatch-trace] suspect-call ctx=%p host_call=%p address=%08X pc=%08X lr=%08X srr0=%08X exception=%08X\n",
                (void*)entry->ctx, (void*)entry->host_call, entry->address, entry->pc,
                entry->lr, entry->srr0, entry->exception);
        bw_trace_dump("suspect-call");
    }
}

void bw_trace_dispatch(const void* host, const void* module, const void* dispatch) {
    BwTailTrace* entry = &g_tails[g_tail_count % BW_TRACE_RING];
    entry->host = (uintptr_t)host;
    entry->module = (uintptr_t)module;
    entry->dispatch = (uintptr_t)dispatch;
    g_tail_count++;
    if (implausible_function(entry->dispatch) || implausible_data(entry->module)) {
        fprintf(stderr, "[dispatch-trace] suspect-dispatch host=%p module=%p dispatch=%p\n",
                host, module, dispatch);
        bw_trace_dump("suspect-dispatch");
    }
}

void bw_trace_dump(const char* why) {
    if (why != NULL && strcmp(why, "suspect-call") == 0) {
        if (g_suspect_call_dumps >= 4u)
            return;
        g_suspect_call_dumps++;
    } else if (why != NULL && strcmp(why, "suspect-dispatch") == 0) {
        if (g_suspect_dispatch_dumps >= 4u)
            return;
        g_suspect_dispatch_dumps++;
    } else if (why != NULL && g_last_why != NULL && strcmp(why, g_last_why) == 0) {
        return;
    }
    g_last_why = why;
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
    fflush(stderr);
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
