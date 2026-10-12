#ifndef BLUEWAKE_SCHEDULER_CONTRACT_H
#define BLUEWAKE_SCHEDULER_CONTRACT_H

#include "core/cpu.h"

typedef struct BluewakeSchedulerIdleState {
    u32 run_bits;
    u32 current_thread;
    u32 current_context;
} BluewakeSchedulerIdleState;

static inline BluewakeSchedulerIdleState bluewake_scheduler_idle_state(CPUState* cpu) {
    const BluewakeSchedulerIdleState state = {mem_read32(cpu, 0x803F7A30u),
                                              mem_read32(cpu, 0x800000E4u),
                                              mem_read32(cpu, 0x800000D4u)};
    return state;
}

// Materialize a host-delivered exception into the current guest OSContext.
// This preserves an OSSaveContext coroutine return that guest code committed
// before the translated CPU shadow reached the continuation.
void bluewake_scheduler_save_exception_context(CPUState* cpu, u32 context);

// Guest scheduler/context operations that span multiple translated dispatches
// are not architectural interrupt boundaries.
bool bluewake_scheduler_interrupt_safe(u32 pc);

// A chained generated edge must return to the host as soon as an already
// published external interrupt becomes deliverable at the new guest PC.
bool bluewake_scheduler_interrupt_requires_host(u32 pc, u32 msr,
                                                bool external_pending);

#endif
