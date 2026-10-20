/* Tests-only scenarios for the dispatch ring. The game-free build has no
 * module, so the probe drives the ring directly; the real dispatcher calls the
 * same record/settle pair from cmake/composite/module_export.c. */
#include "dispatch_ring.h"

#include <stdio.h>
#include <string.h>

static void fill(unsigned count, uint32_t first_pc, uint32_t sp) {
    for (unsigned index = 0u; index < count; ++index) {
        bw_dispatch_ring_record(first_pc + 4u * index, sp, 0x80003100u + index);
        bw_dispatch_ring_settle(true);
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s SCENARIO\n", argv[0]);
        return 2;
    }
    const char* const scenario = argv[1];
    bw_dispatch_ring_reset();

    if (strcmp(scenario, "wrap") == 0) {
        fill(40u, 0x80003100u, 0x80170C40u);
        printf("[probe] count=%u\n", bw_dispatch_ring_length());
        const BwDispatchEntry* first = bw_dispatch_ring_entry(0u);
        const BwDispatchEntry* last = bw_dispatch_ring_entry(bw_dispatch_ring_length() - 1u);
        printf("[probe] first_pc=%08X last_pc=%08X\n", first->pc, last->pc);
        return bw_dispatch_ring_dump(stdout, "wrap") ? 0 : 1;
    }
    if (strcmp(scenario, "routed-empty") == 0) {
        bw_dispatch_ring_record(0x80003100u, 0x80170C40u, 1u);
        bw_dispatch_ring_settle(false);
        printf("[probe] count=%u\n", bw_dispatch_ring_length());
        return 0;
    }
    if (strcmp(scenario, "routed-physical-alias") == 0) {
        bw_dispatch_ring_record(0x80003100u, 0x80170C40u, 0x80003100u);
        bw_dispatch_ring_settle(true);
        bw_dispatch_ring_record(0x00305C24u, 0x80170C40u, 0x80003104u);
        bw_dispatch_ring_settle(true);
        printf("[probe] count=%u\n", bw_dispatch_ring_length());
        return 0;
    }
    if (strcmp(scenario, "unrouted-after-entry") == 0) {
        bw_dispatch_ring_record(0x800396E0u, 0x80170C40u, 0x80003100u);
        bw_dispatch_ring_settle(true);
        bw_dispatch_ring_record(0xDEAD8000u, 0x80170C40u, 0x80003104u);
        bw_dispatch_ring_settle(false);
        fprintf(stderr, "unreachable: unrouted dispatch returned\n");
        return 1;
    }
    if (strcmp(scenario, "stack-before-settled") == 0) {
        fill(7u, 0x80003100u, 0x8015FFF0u);
        printf("[probe] count=%u\n", bw_dispatch_ring_length());
        return 0;
    }
    if (strcmp(scenario, "stack-arena-boot") == 0) {
        fill(40u, 0x80003100u, 0x8040CFA0u);
        printf("[probe] count=%u\n", bw_dispatch_ring_length());
        return 0;
    }
    if (strcmp(scenario, "stack-settled") == 0) {
        fill(7u, 0x80003100u, 0x80170C40u);
        bw_dispatch_ring_record(0x8000311Cu, 0x8015FFF0u, 0x80003108u);
        fprintf(stderr, "unreachable: settled out-of-band stack returned\n");
        return 1;
    }
    if (strcmp(scenario, "stack-band-then-drift") == 0) {
        fill(3u, 0x80003100u, 0x8040CFA0u);
        for (unsigned index = 0u; index < 4u; ++index)
            bw_dispatch_ring_record(0x8000310Cu + 4u * index, 0x80170C40u, 0x80003103u);
        bw_dispatch_ring_record(0x8000311Cu, 0x8015FFF0u, 0x80003107u);
        fprintf(stderr, "unreachable: drifted stack after the module stack returned\n");
        return 1;
    }
    if (strcmp(scenario, "stack-band-edges") == 0) {
        fill(8u, 0x80003100u, BW_GUEST_STACK_LOW);
        bw_dispatch_ring_record(0x80003120u, BW_GUEST_STACK_HIGH - 1u, 0x80003109u);
        bw_dispatch_ring_settle(true);
        printf("[probe] count=%u\n", bw_dispatch_ring_length());
        return 0;
    }
    if (strcmp(scenario, "stopped-silent") == 0) {
        fill(1u, 0x80003100u, 0x80170C40u);
        bw_dispatch_ring.stopped = true;
        bw_dispatch_ring_record(0xDEAD8000u, 0x8015FFF0u, 1u);
        bw_dispatch_ring_settle(false);
        printf("[probe] count=%u\n", bw_dispatch_ring_length());
        return 0;
    }
    fprintf(stderr, "unknown scenario %s\n", scenario);
    return 2;
}
