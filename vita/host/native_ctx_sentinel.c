#include "native_ctx.h"

#include <string.h>

_Static_assert(sizeof(WwAbiSnapshot) == 112u, "dispatch snapshot ABI");
_Static_assert(offsetof(WwAbiSnapshot, sp) == 96u, "dispatch SP offset");
_Static_assert(offsetof(WwAbiPair, before) == 16u, "dispatch entry offset");
_Static_assert(offsetof(WwAbiPair, after) == 128u, "dispatch return offset");
_Static_assert(sizeof(WwAbiPair) == 240u, "dispatch pair ABI");
_Static_assert(offsetof(WwAbiDump, failure) == 36u, "dispatch failure offset");
_Static_assert(WW_ABI_INTERNAL == 128u, "dispatch internal bit");

WwAbiPair ww_abi_active;
WwAbiDump ww_abi_dump;
uint32_t ww_abi_state;
static WwNativeWriteAt writer;
static void* writer_user;

/* Includes the independently anchored capsule, callbacks and a conservative helper stack allowance. */
_Static_assert(sizeof(ww_abi_active) + sizeof(ww_abi_dump) + sizeof(ww_abi_state) +
               sizeof(writer) + sizeof(writer_user) + 512u <= 8192u, "total dispatch storage bound");

void ww_abi_init(uint32_t expected_ctx, WwNativeWriteAt write_at, void* user) {
    memset(&ww_abi_active, 0, sizeof(ww_abi_active));
    memset(&ww_abi_dump, 0, sizeof(ww_abi_dump));
    ww_abi_state = 0u;
    writer = write_at;
    writer_user = user;
    ww_abi_dump.magic = 0x49424157u;
    ww_abi_dump.version = 1u;
    ww_abi_dump.bytes = sizeof(ww_abi_dump) + sizeof(ww_abi_active);
    ww_abi_dump.expected_ctx = expected_ctx;
}

void ww_abi_arm(uint32_t module_id, uint32_t target) {
    if (module_id != 168u || ww_abi_state != 0u)
        return;
    ww_abi_dump.expected_target = target;
    ww_abi_dump.milestone = module_id;
    ww_abi_state = 1u;
}

static bool stop(uint32_t failure) {
    ww_abi_state = 2u;
    ww_abi_dump.failure = failure;
    if (writer == NULL ||
        writer(writer_user, 0u, &ww_abi_dump, sizeof(ww_abi_dump)) != sizeof(ww_abi_dump) ||
        writer(writer_user, sizeof(ww_abi_dump), &ww_abi_active, sizeof(ww_abi_active)) != sizeof(ww_abi_active)) {
        ww_abi_dump.io_error = 1u;
        return false;
    }
    const uint32_t complete = WW_ABI_COMPLETE;
    if (writer(writer_user, offsetof(WwAbiDump, complete), &complete, sizeof(complete)) != sizeof(complete)) {
        ww_abi_dump.io_error = 1u;
        return false;
    }
    ww_abi_dump.complete = WW_ABI_COMPLETE;
    return false;
}

bool ww_abi_internal_capture(const WwAbiSnapshot* before, const WwAbiSnapshot* after) {
    const uintptr_t finish = (uintptr_t)&ww_abi_finish;
    return ((uintptr_t)after->lr >= finish && (uintptr_t)after->lr < finish + WW_ABI_FINISH_SPAN) ||
           after->sp == before->sp - 24u;
}

const char* ww_abi_stop_reason(uint32_t failure, uint32_t io_error) {
    if (io_error)
        return "dispatch ABI diagnostic incomplete: capsule write failed";
    if (failure & WW_ABI_INTERNAL)
        return "dispatch ABI mismatch: internal finish-frame capture";
    return "dispatch ABI mismatch: diagnostic stop";
}

bool ww_abi_begin(void) {
    if (ww_abi_state != 1u)
        return false;
    if (ww_abi_dump.invocations == UINT64_MAX)
        return stop(WW_ABI_OVERFLOW);
    ww_abi_active.invocation = ++ww_abi_dump.invocations;
    ww_abi_active.returned = 0u;
    memset(&ww_abi_active.after, 0, sizeof(ww_abi_active.after));
    uint32_t failure = 0u;
    if (ww_abi_dump.expected_ctx == 0u || ww_abi_active.before.r0 != ww_abi_dump.expected_ctx)
        failure |= WW_ABI_CONTEXT;
    const uint32_t target = ww_abi_active.target;
    if (target != ww_abi_dump.expected_target || (target & ~1u) == 0u ||
        (!(target & 1u) && (target & 3u)))
        failure |= WW_ABI_TARGET;
    if (ww_abi_active.before.sp & 7u)
        failure |= WW_ABI_SP;
    if (failure)
        return stop(failure);
    ww_abi_state = 3u;
    return true;
}

bool ww_abi_finish(void) {
    if (ww_abi_state != 3u && ww_abi_state != 4u)
        return false;
    ww_abi_active.returned = 1u;
    uint32_t failure = ww_abi_state == 4u ? WW_ABI_REENTRY : 0u;
    if (ww_abi_active.before.sp != ww_abi_active.after.sp)
        failure |= WW_ABI_SP;
    if (memcmp(ww_abi_active.before.r, ww_abi_active.after.r, sizeof(ww_abi_active.before.r)) != 0)
        failure |= WW_ABI_REGISTER;
    if (memcmp(ww_abi_active.before.d, ww_abi_active.after.d, sizeof(ww_abi_active.before.d)) != 0)
        failure |= WW_ABI_VFP;
    if (ww_abi_internal_capture(&ww_abi_active.before, &ww_abi_active.after))
        failure |= WW_ABI_INTERNAL;
    if (failure)
        return stop(failure);
    volatile unsigned char* dst = (volatile unsigned char*)&ww_abi_dump.pairs[ww_abi_dump.next];
    const volatile unsigned char* src = (const volatile unsigned char*)&ww_abi_active;
    for (size_t i = 0; i < sizeof(WwAbiPair); ++i)
        dst[i] = src[i];
    ww_abi_dump.next = (ww_abi_dump.next + 1u) % 16u;
    if (ww_abi_dump.count < 16u)
        ++ww_abi_dump.count;
    ww_abi_state = 1u;
    return true;
}
