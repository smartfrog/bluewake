#include "native_ctx.h"

#include <stdatomic.h>
#include <string.h>

_Static_assert(sizeof(WwNativeRecord) == 128, "trace record ABI");
_Static_assert(sizeof(WwNativeSnapshot) == 160, "assembly snapshot ABI");
_Static_assert(offsetof(WwNativeSnapshot, d) == 64, "VFP snapshot offset");
_Static_assert(offsetof(WwNativeSnapshot, target) == 132, "target snapshot offset");
_Static_assert(4096 + (4096 + 512) * 128 == WW_NATIVE_FILE_BYTES, "trace bound");

static struct {
    void* host;
    void* cpu;
    const uint32_t* pc;
    WwNativeWriteAt write_at;
    void* user;
    uint32_t thread, sequence, calls, pre, post, depth, pinned_call, last_sp;
    bool armed, pinned, failure_attempted;
    WwNativeRecord chunk, entries[WW_NATIVE_MAX_DEPTH];
} trace;
static atomic_flag busy = ATOMIC_FLAG_INIT;
static atomic_uint failures;
atomic_uint ww_native_active;

typedef struct {
    uint32_t live, seen, lr, sp;
} WwCtxGuardHit;

static WwCtxGuardHit guard_ring[WW_NATIVE_CTX_GUARD_RING];
static uint32_t guard_hits, guard_next;
static bool guard_stopped;

__attribute__((weak)) int ww_native_guard_marker(uint32_t hit, uint32_t pc, uint32_t live,
                                                  uint32_t seen, uint32_t lr, uint32_t sp) {
    (void)hit; (void)pc; (void)live; (void)seen; (void)lr; (void)sp;
    return 0;
}

static uint32_t address(const void* pointer) {
    return (uint32_t)(uintptr_t)pointer;
}

static uint32_t checksum(const WwNativeRecord* record) {
    uint32_t value = 2166136261u;
    const unsigned char* bytes = (const unsigned char*)record;
    for (size_t index = 0; index < offsetof(WwNativeRecord, checksum); ++index)
        value = (value ^ bytes[index]) * 16777619u;
    return value;
}

bool ww_native_record_valid(const WwNativeRecord* record) {
    return record->magic == WW_NATIVE_MAGIC && record->version == 1u &&
        record->inverse_sequence == ~record->sequence && record->checksum == checksum(record);
}

static bool write_record(uint32_t offset, WwNativeRecord* record) {
    record->checksum = checksum(record);
    if (trace.write_at == NULL || trace.write_at(trace.user, offset, record, sizeof(*record)) != sizeof(*record)) {
        atomic_fetch_or(&failures, WW_NATIVE_IO_FAILURE);
        return false;
    }
    return true;
}

static WwNativeRecord make_record(uint32_t stage, uint32_t surface) {
    WwNativeRecord record = {.magic = WW_NATIVE_MAGIC, .version = 1u,
        .sequence = ++trace.sequence, .stage = stage, .surface = surface,
        .expected_ctx = address(trace.cpu)};
    record.inverse_sequence = ~record.sequence;
    if (trace.pc != NULL) {
        record.expected_pc = *trace.pc;
        record.flags |= WW_NATIVE_EXPECTED_PC_VALID;
    }
    return record;
}

static void failure_marker(void) {
    const uint32_t flags = atomic_load(&failures);
    if (flags == 0u || trace.failure_attempted)
        return;
    trace.failure_attempted = true;
    WwNativeRecord record = make_record(WW_NATIVE_FAILURE, 0u);
    record.flags |= flags;
    (void)write_record(640u, &record);
    if (ww_native_failure_notify(atomic_load(&failures)) < 0)
        atomic_fetch_or(&failures, WW_NATIVE_IO_FAILURE);
}

static bool lock_trace(void) {
    if (ww_native_thread_id() != trace.thread) {
        atomic_fetch_or(&failures, WW_NATIVE_CONCURRENCY);
        return false;
    }
    if (atomic_flag_test_and_set(&busy)) {
        atomic_fetch_or(&failures, WW_NATIVE_REENTRANCY);
        return false;
    }
    return true;
}

static void unlock_trace(void) {
    failure_marker();
    atomic_flag_clear(&busy);
}

static void append(WwNativeRecord* record) {
    if (!trace.pinned) {
        (void)write_record(WW_NATIVE_HEADER_BYTES + trace.pre * WW_NATIVE_RECORD_BYTES, record);
        trace.pre = (trace.pre + 1u) % WW_NATIVE_PRE_RECORDS;
    } else {
        (void)write_record(WW_NATIVE_HEADER_BYTES +
            (WW_NATIVE_PRE_RECORDS + trace.post) * WW_NATIVE_RECORD_BYTES, record);
        trace.post = (trace.post + 1u) % WW_NATIVE_POST_RECORDS;
    }
}

static void pin(WwNativeRecord* mismatch, WwNativeRecord* entry) {
    if (trace.pinned)
        return;
    trace.pinned = true;
    if (trace.chunk.magic != 0u)
        (void)write_record(128u, &trace.chunk);
    if (entry != NULL) {
        trace.pinned_call = entry->call_id;
        (void)write_record(256u, entry);
    }
    if (mismatch->stage == WW_NATIVE_RETURN)
        (void)write_record(384u, mismatch);
    (void)write_record(512u, mismatch);
}

static void qualify(WwNativeRecord* record, uint32_t pointer, bool host_pointer) {
    const uint32_t expected = address(host_pointer ? trace.host : trace.cpu);
    if (expected == 0u || pointer != expected) {
        record->flags |= WW_NATIVE_CTX_MISMATCH;
    } else if (trace.pc != NULL) {
        record->pc = *trace.pc;
        record->flags |= WW_NATIVE_PC_VALID;
    }
}

static bool has_ctx(uint32_t surface) {
    return surface != WW_NATIVE_ALIAS && surface != WW_NATIVE_PREAD && surface != WW_NATIVE_JOURNAL;
}

void ww_native_trace_init(void* host, void* cpu, const uint32_t* pc,
                          WwNativeWriteAt write_at, void* user, uint32_t thread) {
    memset(&trace, 0, sizeof(trace));
    atomic_store(&ww_native_active, 0u);
    atomic_store(&failures, 0u);
    atomic_flag_clear(&busy);
    trace.host = host;
    trace.cpu = cpu;
    trace.pc = pc;
    trace.write_at = write_at;
    trace.user = user;
    trace.thread = thread;
    guard_hits = guard_next = 0u;
    guard_stopped = false;
    memset(guard_ring, 0, sizeof(guard_ring));
    WwNativeRecord header = make_record(0u, 0u);
    header.r[0] = WW_NATIVE_FILE_BYTES;
    header.r[1] = WW_NATIVE_PRE_RECORDS;
    header.r[2] = WW_NATIVE_POST_RECORDS;
    header.r[3] = WW_NATIVE_MAX_DEPTH;
    (void)write_record(0u, &header);
    failure_marker();
}

void ww_native_trace_arm(uint32_t module_id) {
    if (module_id != 400u || !lock_trace())
        return;
    if (!trace.armed) {
        trace.armed = true;
        atomic_store(&ww_native_active, 1u);
        WwNativeRecord record = make_record(WW_NATIVE_ARMED, 0u);
        record.r[0] = module_id;
        append(&record);
        (void)write_record(768u, &record);
    }
    unlock_trace();
}

void ww_native_trace_marker(uint32_t stage, uint32_t first, uint32_t second, uint32_t third) {
    if (!atomic_load(&ww_native_active))
        return;
    if (!lock_trace())
        return;
    if (trace.armed) {
        WwNativeRecord record = make_record(stage, 0u);
        record.r[0] = first;
        record.r[1] = second;
        record.r[2] = third;
        append(&record);
        if (stage >= WW_NATIVE_OPEN && stage <= WW_NATIVE_BODY_READ)
            (void)write_record(896u + (stage - WW_NATIVE_OPEN) * 128u, &record);
        if (stage == WW_NATIVE_JOURNAL_STATE)
            (void)write_record(1280u, &record);
    }
    unlock_trace();
}

void ww_native_trace_enter(WwNativeSnapshot* snapshot) {
    snapshot->token = 0u;
    if (!atomic_load(&ww_native_active))
        return;
    if (!lock_trace())
        return;
    if (!trace.armed) {
        unlock_trace();
        return;
    }
    const bool chunk = snapshot->surface == WW_NATIVE_CHUNK_ENTRY;
    if (!chunk && trace.depth == WW_NATIVE_MAX_DEPTH) {
        atomic_fetch_or(&failures, WW_NATIVE_DEPTH_FAILURE);
        unlock_trace();
        return;
    }
    WwNativeRecord record = make_record(chunk ? WW_NATIVE_CHUNK : WW_NATIVE_ENTRY, snapshot->surface);
    record.call_id = ++trace.calls;
    record.target = snapshot->target;
    record.wrapper = snapshot->wrapper;
    record.sp = snapshot->sp;
    record.continuation = snapshot->lr;
    memcpy(record.r, snapshot->r, sizeof(record.r));
    memcpy(record.stack, snapshot->stack, sizeof(record.stack));
    if (has_ctx(record.surface))
        qualify(&record, record.r[record.surface == WW_NATIVE_EDGE ? 1u : 0u], false);
    if (chunk) {
        trace.chunk = record;
    } else {
        snapshot->token = record.call_id;
        trace.entries[trace.depth++] = record;
    }
    if (record.flags & WW_NATIVE_CTX_MISMATCH)
        pin(&record, chunk ? NULL : &record);
    append(&record);
    if (record.surface == WW_NATIVE_FCMP || record.surface == WW_NATIVE_FCTIW ||
        record.surface == WW_NATIVE_FRSQRTE) {
        record = make_record(WW_NATIVE_FP_ARGS, snapshot->surface);
        record.call_id = snapshot->token;
        memcpy(record.r, snapshot->d, 16u);
        append(&record);
    }
    unlock_trace();
}

void ww_native_trace_return(const WwNativeSnapshot* entry, const WwNativeSnapshot* returned) {
    if (entry->token == 0u || !atomic_load(&ww_native_active) || !lock_trace())
        return;
    if (trace.depth == 0u || trace.entries[trace.depth - 1u].call_id != entry->token) {
        atomic_fetch_or(&failures, WW_NATIVE_PAIR_FAILURE);
        unlock_trace();
        return;
    }
    WwNativeRecord* before = &trace.entries[--trace.depth];
    trace.last_sp = entry->sp;
    WwNativeRecord record = make_record(WW_NATIVE_RETURN, entry->surface);
    record.call_id = entry->token;
    record.target = entry->target;
    record.wrapper = entry->wrapper;
    record.sp = returned->sp;
    record.continuation = entry->lr;
    memcpy(record.r, returned->r, sizeof(record.r));
    memcpy(record.stack, entry->stack, sizeof(record.stack));
    if (returned->r[4] != entry->r[4])
        record.flags |= WW_NATIVE_R4_CHANGED;
    if (trace.pinned && trace.pinned_call == entry->token)
        (void)write_record(384u, &record);
    if (record.flags & WW_NATIVE_R4_CHANGED)
        pin(&record, before);
    append(&record);
    unlock_trace();
}

void ww_native_ctx_guard(uint32_t live, uint32_t seen, uint32_t lr) {
    if (live == seen || guard_stopped || !atomic_load(&ww_native_active) || !lock_trace())
        return;
    const uint32_t hit = ++guard_hits;
    guard_ring[guard_next] = (WwCtxGuardHit){live, seen, lr, trace.last_sp};
    guard_next = (guard_next + 1u) % WW_NATIVE_CTX_GUARD_RING;
    if (hit == 1u) {
        WwNativeRecord record = make_record(WW_NATIVE_RETURN, 0u);
        record.r[0] = live;
        record.r[4] = seen;
        record.sp = trace.last_sp;
        record.continuation = lr;
        if (trace.pc != NULL) {
            record.pc = *trace.pc;
            record.flags |= WW_NATIVE_PC_VALID;
        }
        record.flags |= WW_NATIVE_CTX_MISMATCH | WW_NATIVE_R4_CHANGED;
        (void)write_record(384u, &record);
    }
    if (hit <= WW_NATIVE_CTX_GUARD_HITS) {
        const uint32_t pc = trace.pc != NULL ? *trace.pc : 0u;
        if (ww_native_guard_marker(hit, pc, live, seen, lr, trace.last_sp) < 0)
            atomic_fetch_or(&failures, WW_NATIVE_IO_FAILURE);
    } else {
        guard_stopped = true;
        atomic_fetch_or(&failures, WW_NATIVE_CTX_GUARD);
        if (trace.failure_attempted && ww_native_failure_notify(WW_NATIVE_CTX_GUARD) < 0)
            atomic_fetch_or(&failures, WW_NATIVE_IO_FAILURE);
    }
    unlock_trace();
}

void ww_native_trace_boundary(const void* pointer, uint32_t surface) {
    if (!atomic_load(&ww_native_active))
        return;
    if (!lock_trace())
        return;
    if (trace.armed) {
        WwNativeRecord record = make_record(WW_NATIVE_BOUNDARY, surface);
        record.r[0] = address(pointer);
        qualify(&record, record.r[0], surface == WW_NATIVE_OUTER_DISPATCH);
        if (record.flags & WW_NATIVE_CTX_MISMATCH)
            pin(&record, NULL);
        append(&record);
    }
    unlock_trace();
}

uint32_t ww_native_trace_failures(void) {
    return atomic_load(&failures);
}

void ww_native_trace_failure(uint32_t flags) {
    atomic_fetch_or(&failures, flags);
    if (lock_trace())
        unlock_trace();
}

void ww_native_trace_end(void) {
    if (!lock_trace())
        return;
    trace.armed = false;
    atomic_store(&ww_native_active, 0u);
    trace.pc = NULL;
    unlock_trace();
}
