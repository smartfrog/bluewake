#include "dispatch_ring.h"

#include <stdlib.h>

BwDispatchRing bw_dispatch_ring;

static const char* tripwire_name(BwDispatchTripwire trip) {
    switch (trip) {
    case BW_DISPATCH_TRIP_UNRESOLVED_PC: return "unresolved-pc";
    case BW_DISPATCH_TRIP_STACK_OUT_OF_BAND: return "stack-out-of-band";
    case BW_DISPATCH_TRIP_NONE: break;
    }
    return "none";
}

unsigned bw_dispatch_ring_length(void) {
    return bw_dispatch_ring.count;
}

const BwDispatchEntry* bw_dispatch_ring_entry(unsigned ordinal) {
    if (ordinal >= bw_dispatch_ring.count)
        return NULL;
    const unsigned oldest =
        (bw_dispatch_ring.count < BW_DISPATCH_RING_CAPACITY) ? 0u : bw_dispatch_ring.next;
    return &bw_dispatch_ring.entries[(oldest + ordinal) % BW_DISPATCH_RING_CAPACITY];
}

bool bw_dispatch_ring_dump(FILE* out, const char* tag) {
    if (out == NULL)
        return false;
    bool ok = fprintf(out, "[dispatch-ring] dump tag=%s entries=%u capacity=%u\n",
                      tag, bw_dispatch_ring.count, BW_DISPATCH_RING_CAPACITY) >= 0;
    for (unsigned ordinal = 0u; ordinal < bw_dispatch_ring.count; ++ordinal) {
        const BwDispatchEntry* entry = bw_dispatch_ring_entry(ordinal);
        ok = fprintf(out, "[dispatch-ring] entry=%02u pc=%08X sp=%08X lr=%08X\n",
                     ordinal, entry->pc, entry->sp, entry->lr) >= 0 && ok;
    }
    return fflush(out) == 0 && ok;
}

void bw_dispatch_ring_stop(BwDispatchTripwire trip, uint32_t pc, uint32_t sp, uint32_t lr) {
    bw_dispatch_ring.stopped = true;
    const char* const reason = tripwire_name(trip);
    if (fprintf(stderr,
                "[dispatch-ring] tripwire=%s reason=dispatch-entry entries=%u\n"
                "[dispatch-ring] offending pc=%08X sp=%08X lr=%08X\n"
                "[dispatch-ring] guest pc=%08X sp=%08X lr=%08X\n",
                reason, bw_dispatch_ring.count, pc, sp, lr, pc, sp, lr) < 0 ||
        !bw_dispatch_ring_dump(stderr, reason))
        fprintf(stderr, "[dispatch-ring] dump write failed\n");
    abort();
}

void bw_dispatch_ring_reset(void) {
    bw_dispatch_ring.count = 0u;
    bw_dispatch_ring.next = 0u;
    bw_dispatch_ring.stopped = false;
    bw_dispatch_ring.module_stack_seen = false;
}
