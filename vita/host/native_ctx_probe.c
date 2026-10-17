#include "native_ctx.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char file[WW_NATIVE_FILE_BYTES];
static uint32_t thread = 1u, pc = 0x802b0470u, calls, mode;
static bool inside;
static uint32_t guard_markers, notified;

uint32_t ww_native_thread_id(void) { return thread; }
void ww_native_probe_set_thread(uint32_t value) { thread = value; }
int ww_native_failure_notify(uint32_t flags) { notified |= flags; return 0; }

int ww_native_guard_marker(uint32_t hit, uint32_t pc_value, uint32_t live, uint32_t seen,
                           uint32_t lr, uint32_t sp) {
    (void)hit; (void)pc_value; (void)live; (void)seen; (void)lr; (void)sp;
    ++guard_markers;
    return 0;
}

static int write_at(void* user, uint32_t offset, const void* bytes, uint32_t size) {
    (void)user;
    assert(offset <= sizeof(file) && size <= sizeof(file) - offset);
    ++calls;
    if (mode == 1u)
        return (int)size - 1;
    if (mode == 2u)
        return -1;
    if (mode == 3u && !inside) {
        inside = true;
        ww_native_trace_boundary((void*)0x1234u, WW_NATIVE_DISPATCH_ENTER);
        inside = false;
    }
    memcpy(file + offset, bytes, size);
    return (int)size;
}

static WwNativeRecord get(uint32_t offset) {
    WwNativeRecord record;
    memcpy(&record, file + offset, sizeof(record));
    return record;
}

static WwNativeSnapshot snapshot(uint32_t surface) {
    WwNativeSnapshot value = {.r = {0x1234u}, .sp = 0x80001000u, .lr = 0x81002003u,
        .surface = surface, .target = 0x81234567u, .wrapper = 0x81000001u};
    value.r[4] = 0x1234u;
    return value;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    memset(file, 0, sizeof(file));
    thread = 1u;
    calls = mode = 0u;
    guard_markers = notified = 0u;
    inside = false;
    const char* scenario = argv[1];
    ww_native_trace_init((void*)0x5678u, (void*)0x1234u, &pc, write_at, NULL, thread);
    if (strcmp(scenario, "unarmed") == 0) {
        WwNativeSnapshot entry = snapshot(WW_NATIVE_READ);
        ww_native_trace_arm(1u);
        ww_native_trace_enter(&entry);
        assert(entry.token == 0u && calls == 1u);
        return 0;
    }
    ww_native_trace_arm(400u);
    WwNativeSnapshot entry = snapshot(WW_NATIVE_READ), returned = entry;
    if (strcmp(scenario, "preserve") == 0 || strcmp(scenario, "clobber") == 0) {
        ww_native_trace_enter(&entry);
        if (strcmp(scenario, "clobber") == 0)
            returned.r[4] = 1u;
        ww_native_trace_return(&entry, &returned);
        WwNativeRecord record = get(strcmp(scenario, "clobber") == 0 ? 384u : 4352u);
        assert(ww_native_record_valid(&record));
        assert(record.r[4] == returned.r[4]);
        assert(!!(record.flags & WW_NATIVE_R4_CHANGED) == (returned.r[4] == 1u));
    } else if (strcmp(scenario, "ctx-one") == 0) {
        entry.r[0] = 1u;
        ww_native_trace_enter(&entry);
        WwNativeRecord record = get(512u);
        assert(ww_native_record_valid(&record) && record.r[0] == 1u);
        assert((record.flags & WW_NATIVE_CTX_MISMATCH) && !(record.flags & WW_NATIVE_PC_VALID));
        ww_native_trace_return(&entry, &returned);
        record = get(384u);
        assert(ww_native_record_valid(&record) && record.call_id == entry.token);
    } else if (strcmp(scenario, "nesting") == 0) {
        WwNativeSnapshot nested = entry;
        ww_native_trace_enter(&entry);
        ww_native_trace_enter(&nested);
        assert(nested.token != entry.token);
        ww_native_trace_return(&nested, &returned);
        ww_native_trace_return(&entry, &returned);
        assert(ww_native_trace_failures() == 0u);
    } else if (strcmp(scenario, "overflow") == 0) {
        WwNativeSnapshot entries[WW_NATIVE_MAX_DEPTH + 1u];
        for (unsigned i = 0; i <= WW_NATIVE_MAX_DEPTH; ++i) {
            entries[i] = entry;
            ww_native_trace_enter(&entries[i]);
        }
        assert(entries[WW_NATIVE_MAX_DEPTH].token == 0u);
        assert(ww_native_trace_failures() == WW_NATIVE_DEPTH_FAILURE);
        for (unsigned i = WW_NATIVE_MAX_DEPTH; i > 0; --i)
            ww_native_trace_return(&entries[i - 1], &returned);
        assert(!(ww_native_trace_failures() & WW_NATIVE_PAIR_FAILURE));
    } else if (strcmp(scenario, "ring") == 0) {
        for (unsigned i = 0; i < WW_NATIVE_PRE_RECORDS + 4u; ++i)
            ww_native_trace_boundary((void*)0x1234u, WW_NATIVE_DISPATCH_ENTER);
        WwNativeRecord record = get(4096u);
        assert(ww_native_record_valid(&record) && record.sequence > WW_NATIVE_PRE_RECORDS);
    } else if (strcmp(scenario, "pinning") == 0) {
        WwNativeSnapshot chunk = snapshot(WW_NATIVE_CHUNK_ENTRY);
        ww_native_trace_enter(&chunk);
        ww_native_trace_enter(&entry);
        returned.r[4] = 1u;
        ww_native_trace_return(&entry, &returned);
        unsigned char pinned[640];
        memcpy(pinned, file, sizeof(pinned));
        for (unsigned i = 0; i < 5000; ++i)
            ww_native_trace_boundary((void*)1u, WW_NATIVE_DISPATCH_ENTER);
        assert(memcmp(pinned, file, sizeof(pinned)) == 0);
        WwNativeRecord record = get(256u);
        assert(record.call_id == entry.token && ww_native_record_valid(&record));
    } else if (strcmp(scenario, "short-write") == 0 || strcmp(scenario, "full-disk") == 0) {
        mode = strcmp(scenario, "short-write") == 0 ? 1u : 2u;
        const uint32_t before = calls;
        ww_native_trace_enter(&entry);
        ww_native_trace_return(&entry, &returned);
        assert(ww_native_trace_failures() == WW_NATIVE_IO_FAILURE);
        assert(calls == before + 3u);
    } else if (strcmp(scenario, "recursion") == 0) {
        mode = 3u;
        ww_native_trace_enter(&entry);
        assert(ww_native_trace_failures() == WW_NATIVE_REENTRANCY);
    } else if (strcmp(scenario, "concurrency") == 0) {
        thread = 2u;
        const uint32_t before = calls;
        ww_native_trace_enter(&entry);
        assert(entry.token == 0u && calls == before);
        thread = 1u;
        ww_native_trace_boundary((void*)0x1234u, WW_NATIVE_DISPATCH_ENTER);
        WwNativeRecord record = get(640u);
        assert(ww_native_record_valid(&record) && (record.flags & WW_NATIVE_CONCURRENCY));
    } else if (strcmp(scenario, "interrupted") == 0) {
        ww_native_trace_enter(&entry);
        WwNativeRecord record = get(4224u);
        assert(ww_native_record_valid(&record));
        record.r[4] ^= 1u;
        assert(!ww_native_record_valid(&record));
    } else if (strcmp(scenario, "markers") == 0) {
        ww_native_trace_marker(WW_NATIVE_OPEN, 1u, 2u, 3u);
        ww_native_trace_marker(WW_NATIVE_HEADER_READ, 2u, 32u, 0u);
        ww_native_trace_marker(WW_NATIVE_BODY_READ, 2u, 382080u, 0u);
        WwNativeRecord record = get(1152u);
        assert(ww_native_record_valid(&record) && record.stage == WW_NATIVE_BODY_READ);
    } else if (strcmp(scenario, "guard-same") == 0) {
        ww_native_ctx_guard(0x1234u, 0x1234u, 0x81002003u);
        assert(guard_markers == 0u && ww_native_trace_failures() == 0u);
        WwNativeRecord record = get(384u);
        assert(record.magic == 0u);
    } else if (strcmp(scenario, "guard-hit") == 0) {
        ww_native_ctx_guard(0x1234u, 0u, 0x802bf744u);
        assert(guard_markers == 1u && ww_native_trace_failures() == 0u);
        WwNativeRecord record = get(384u);
        assert(ww_native_record_valid(&record));
        assert((record.flags & (WW_NATIVE_CTX_MISMATCH | WW_NATIVE_R4_CHANGED)) ==
               (WW_NATIVE_CTX_MISMATCH | WW_NATIVE_R4_CHANGED));
        assert(record.continuation == 0x802bf744u);
        assert(record.pc == pc && (record.flags & WW_NATIVE_PC_VALID));
        assert(record.r[0] == 0x1234u && record.r[4] == 0u);
    } else if (strcmp(scenario, "guard-stop") == 0) {
        for (uint32_t i = 1u; i <= WW_NATIVE_CTX_GUARD_HITS + 1u; ++i)
            ww_native_ctx_guard(0x1234u, i, 0x802bf744u);
        assert(guard_markers == WW_NATIVE_CTX_GUARD_HITS);
        assert(ww_native_trace_failures() == WW_NATIVE_CTX_GUARD);
        assert(notified == WW_NATIVE_CTX_GUARD);
    } else {
        assert(!"unknown scenario");
    }
    puts(scenario);
    return 0;
}
