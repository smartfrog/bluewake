#include "callback_delivery.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define OS_RESCHEDULE 0x803F7A38u

/* The guest has no usable environment (getenv is empty inside the Vita
 * guest), so the probe is gated by a fixture file the guest can open. */
#ifndef BLUEWAKE_CALLBACK_CPU_PROBE_FILE
#define BLUEWAKE_CALLBACK_CPU_PROBE_FILE "ux0:data/bluewake-ww-fixture/ww05.cpu-probe"
#endif

typedef struct GuestRegisterImage {
    u32 gpr[32];
    f64 fpr[32];
    f64 ps1[32];
    u32 pc;
    u32 lr;
    u32 ctr;
    u32 cr;
    u32 xer;
    u32 fpscr;
    u32 msr;
    u32 srr0;
    u32 srr1;
    u32 dar;
    u32 dsisr;
    u32 ear;
    u32 hid2;
    u32 sr[16];
    u32 gqr[8];
    u32 exception;
    u32 program_exception;
} GuestRegisterImage;

static void save_registers(const CPUState* cpu, GuestRegisterImage* image) {
    memcpy(image->gpr, cpu->gpr, sizeof(image->gpr));
    memcpy(image->fpr, cpu->fpr, sizeof(image->fpr));
    memcpy(image->ps1, cpu->ps1, sizeof(image->ps1));
    image->pc = cpu->pc;
    image->lr = cpu->lr;
    image->ctr = cpu->ctr;
    image->cr = cpu->cr;
    image->xer = cpu->xer;
    image->fpscr = cpu->fpscr;
    image->msr = cpu->msr;
    image->srr0 = cpu->srr0;
    image->srr1 = cpu->srr1;
    image->dar = cpu->dar;
    image->dsisr = cpu->dsisr;
    image->ear = cpu->ear;
    image->hid2 = cpu->hid2;
    memcpy(image->sr, cpu->sr, sizeof(image->sr));
    memcpy(image->gqr, cpu->gqr, sizeof(image->gqr));
    image->exception = cpu->exception;
    image->program_exception = cpu->program_exception;
}

static void restore_registers(CPUState* cpu,
                              const GuestRegisterImage* image) {
    memcpy(cpu->gpr, image->gpr, sizeof(image->gpr));
    memcpy(cpu->fpr, image->fpr, sizeof(image->fpr));
    memcpy(cpu->ps1, image->ps1, sizeof(image->ps1));
    cpu->pc = image->pc;
    cpu->lr = image->lr;
    cpu->ctr = image->ctr;
    cpu->cr = image->cr;
    cpu->xer = image->xer;
    cpu->fpscr = image->fpscr;
    cpu->msr = image->msr;
    cpu->srr0 = image->srr0;
    cpu->srr1 = image->srr1;
    cpu->dar = image->dar;
    cpu->dsisr = image->dsisr;
    cpu->ear = image->ear;
    cpu->hid2 = image->hid2;
    memcpy(cpu->sr, image->sr, sizeof(image->sr));
    memcpy(cpu->gqr, image->gqr, sizeof(image->gqr));
    cpu->exception = image->exception;
    cpu->program_exception = image->program_exception;
}

/* Off unless the host asks for it: the loop below hands the CPUState to
 * translated code, so a clobbered callee-saved register shows up as a changed
 * pointer after the call. The first sample proves the probe runs; later ones
 * are printed only when the pointer moved. */
static bool callback_cpu_probe_enabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        struct stat info;
        enabled = lstat(BLUEWAKE_CALLBACK_CPU_PROBE_FILE, &info) == 0 &&
                  S_ISREG(info.st_mode);
        if (enabled)
            fprintf(stderr, "[callback-cpu] probe=on gate=%s\n",
                    BLUEWAKE_CALLBACK_CPU_PROBE_FILE);
    }
    return enabled != 0;
}

static void report_callback_cpu(const char* where, const void* before,
                                u32 before_pc, const void* after, u32 after_pc,
                                const void* caller) {
    static unsigned reported;
    static u64 suppressed;
    static bool seen;
    const bool moved = before != after;
    if (seen && !moved)
        return;
    seen = true;
    if (reported < 8u) {
        reported++;
        fprintf(stderr,
                "[callback-cpu] where=%s before=%p pc=%08X after=%p pc=%08X "
                "lr=%p hits=%u\n",
                where, before, before_pc, after, after_pc, caller, reported);
        return;
    }
    suppressed++;
    if ((suppressed & (suppressed - 1u)) == 0u)
        fprintf(stderr, "[callback-cpu] hits=%u suppressed=%llu\n", reported,
                (unsigned long long)suppressed);
}

BluewakeCallbackDeliveryResult bluewake_deliver_guest_callback(
    CPUState* cpu, const StaticRecompModuleDesc* module, u32 callback,
    u32 arg0, u32 arg1, unsigned max_dispatches,
    BluewakePrepareGuestDispatchFn prepare_dispatch, void* prepare_user) {
    BluewakeCallbackDeliveryResult result = {0};
    GuestRegisterImage interrupted;
    save_registers(cpu, &interrupted);
    const u32 scheduler_depth = mem_read32(cpu, OS_RESCHEDULE);
    mem_write32(cpu, OS_RESCHEDULE, scheduler_depth + 1u);

    /* As the OS's interrupt handlers do (stwu r1,-8(r1) before dispatching):
     * the callback's prologue stores its LR at 4(r1), which a function
     * interrupted between its own LR store and its stwu still needs. */
    cpu->gpr[1] = interrupted.gpr[1] - 8u;
    mem_write32(cpu, cpu->gpr[1], interrupted.gpr[1]);
    cpu->gpr[3] = arg0;
    cpu->gpr[4] = arg1;
    cpu->lr = BLUEWAKE_CALLBACK_RETURN_SENTINEL;
    cpu->pc = callback & ~3u;
    cpu->msr &= ~PPC_MSR_EE;
    cpu->exception = 0u;
    cpu->program_exception = 0u;

    while (cpu->pc != BLUEWAKE_CALLBACK_RETURN_SENTINEL &&
           result.dispatches < max_dispatches) {
        if (prepare_dispatch != NULL) {
            const CPUState* const entered = cpu;
            const u32 entered_pc = cpu->pc;
            prepare_dispatch(cpu, prepare_user);
            if (callback_cpu_probe_enabled())
                report_callback_cpu("prepare", entered, entered_pc, cpu,
                                    cpu == entered ? cpu->pc : 0xFFFFFFFFu,
                                    __builtin_return_address(0));
        }
        const CPUState* const entered = cpu;
        const u32 entered_pc = cpu->pc;
        const int dispatched = module->dispatch(cpu, entered_pc);
        if (callback_cpu_probe_enabled())
            report_callback_cpu("dispatch", entered, entered_pc, cpu,
                                cpu == entered ? cpu->pc : 0xFFFFFFFFu,
                                __builtin_return_address(0));
        if (dispatched != 1)
            break;
        result.dispatches++;
        if (cpu->exception != 0u)
            break;
    }

    result.completed = cpu->pc == BLUEWAKE_CALLBACK_RETURN_SENTINEL &&
                       cpu->exception == 0u;
    result.terminal_pc = cpu->pc;
    result.exception = cpu->exception;
    mem_write32(cpu, OS_RESCHEDULE, scheduler_depth);
    restore_registers(cpu, &interrupted);
    return result;
}
