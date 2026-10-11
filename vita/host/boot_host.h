#ifndef WW05_BOOT_HOST_H
#define WW05_BOOT_HOST_H

#include "module_exports.h"
#include "module_cpu_contract.h"
#include "gx_checkpoint.h"
#include "code_guard.h"
#include "cycle_domain.h"
#include "ipl_sram.h"
#include "gxruntime/di.h"
#include "gxruntime/exi.h"
#include "gxruntime/interrupts.h"
#include "gxruntime/si.h"
#include "gxruntime/audio_dma.h"

typedef struct WwHost {
    CPUState cpu;
    DolGuestMemory memory;
    DolGxRecompState gx;
    WwGxCheckpoint frontier;
    BluewakeCycleDomain cycles;
    DolInterrupts interrupts;
    DolDi di;
    DolExi exi;
    BluewakeIplSram sram;
    DolSiDevice si;
    DolAudioDma audio;
    const StaticRecompModuleDesc* module;
    u32 l2cr;
    WwCodeGuard code_guard;
    const char* error;
    u32 error_address;
    u32 error_pc;
    u8 error_width;
    u64 error_value;
    u32 clock_remainder;
    u32 decrementer;
    u32 hid0;
    u16 mi_interrupt_mask;
    u16 dsp_interrupt_mask;
    u64 alias_bytes;
    u64 backing_bytes;
    u32 alias_count;
    u64 edges;
    u64 next_edge_progress;
    u64 syscall_resumes;
    u32 dispatch_trace_remaining;
    bool edge_stopped;
    bool decrementer_valid;
    bool decrementer_pending;
} WwHost;

static inline bool ww_host_trace_syscall(u64 resume) {
    return resume <= 1u || resume % 1000u == 0u;
}

bool ww_host_init(WwHost* host);
void ww_host_shutdown(WwHost* host);
bool ww_host_load(WwHost* host, const StaticRecompModuleDesc* module,
                   const BlueWakeRelData* data, u32 count, const char* dol);
bool ww_host_edge(void* user, CPUState* cpu, u32 address);
bool ww_host_resume_exception(WwHost* host);
void ww_host_fail(WwHost* host, const char* error, u32 address, u32 pc);

#endif
