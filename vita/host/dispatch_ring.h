/* Bounded ring of the last translated-block dispatches.
 *
 * The module's dispatcher (cmake/composite/module_export.c) records every entry
 * before the block runs and settles the entry with the dispatcher's own result.
 * When a dispatch routes nowhere, or the guest stack leaves the module stack
 * band after having used it, the ring is dumped once and the guest stops. The
 * values are those the dispatcher saw, so an abnormal entry or a drifted SP is
 * visible in the entries that preceded it.
 */
#ifndef WW05_DISPATCH_RING_H
#define WW05_DISPATCH_RING_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define BW_DISPATCH_RING_CAPACITY 32u
#define BW_DISPATCH_RING_SETTLED_ENTRIES 8u
#define BW_GUEST_STACK_LOW 0x80170000u
#define BW_GUEST_STACK_HIGH 0x80180000u

typedef struct BwDispatchEntry {
    uint32_t pc;
    uint32_t sp;
    uint32_t lr;
} BwDispatchEntry;

typedef struct BwDispatchRing {
    BwDispatchEntry entries[BW_DISPATCH_RING_CAPACITY];
    unsigned count;
    unsigned next;
    bool stopped;
    bool module_stack_seen;
} BwDispatchRing;

typedef enum BwDispatchTripwire {
    BW_DISPATCH_TRIP_NONE = 0,
    BW_DISPATCH_TRIP_UNRESOLVED_PC,
    BW_DISPATCH_TRIP_STACK_OUT_OF_BAND,
} BwDispatchTripwire;

extern BwDispatchRing bw_dispatch_ring;

/* Prints the entries oldest first, then the reason and the offending and
 * current guest values, and stops the guest. Never returns. */
void bw_dispatch_ring_stop(BwDispatchTripwire trip, uint32_t pc, uint32_t sp, uint32_t lr);

/* One record per dispatch: three stores and one compare. The stack check waits
 * for a settled ring (eight entries) that has already run on the module stack:
 * the boot stack lives in the arena, outside the band, and is not drift. */
static inline void bw_dispatch_ring_record(uint32_t pc, uint32_t sp, uint32_t lr) {
    BwDispatchRing* const ring = &bw_dispatch_ring;
    if (ring->stopped)
        return;
    const unsigned prior = ring->count;
    ring->entries[ring->next].pc = pc;
    ring->entries[ring->next].sp = sp;
    ring->entries[ring->next].lr = lr;
    ring->next = (ring->next + 1u) % BW_DISPATCH_RING_CAPACITY;
    if (ring->count < BW_DISPATCH_RING_CAPACITY)
        ring->count = prior + 1u;

    const bool in_band = sp >= BW_GUEST_STACK_LOW && sp < BW_GUEST_STACK_HIGH;
    if (in_band)
        ring->module_stack_seen = true;
    else if (ring->module_stack_seen &&
             ring->count >= BW_DISPATCH_RING_SETTLED_ENTRIES)
        bw_dispatch_ring_stop(BW_DISPATCH_TRIP_STACK_OUT_OF_BAND, pc, sp, lr);
}

/* The dispatcher's own result: zero means replacement, host call, translation
 * cache and both physical/REL aliases all declined the pc. Ignored while the
 * ring holds only the entry being settled (pre-adoption noise). */
static inline void bw_dispatch_ring_settle(bool routed) {
    BwDispatchRing* const ring = &bw_dispatch_ring;
    if (ring->stopped || routed || ring->count < 2u)
        return;
    const BwDispatchEntry* const entry =
        &ring->entries[(ring->next + BW_DISPATCH_RING_CAPACITY - 1u) % BW_DISPATCH_RING_CAPACITY];
    bw_dispatch_ring_stop(BW_DISPATCH_TRIP_UNRESOLVED_PC, entry->pc, entry->sp, entry->lr);
}

/* Bounded print: one header line plus one line per held entry (at most
 * BW_DISPATCH_RING_CAPACITY). Returns false if the output failed. */
bool bw_dispatch_ring_dump(FILE* out, const char* tag);

unsigned bw_dispatch_ring_length(void);
const BwDispatchEntry* bw_dispatch_ring_entry(unsigned ordinal);
void bw_dispatch_ring_reset(void);

#endif
