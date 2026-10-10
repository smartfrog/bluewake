#ifndef WW05_BOOT_HOST_H
#define WW05_BOOT_HOST_H

#include "module_exports.h"
#include "module_cpu_contract.h"
#include "gx_checkpoint.h"
#include "cycle_domain.h"
#include "gxruntime/di.h"
#include "gxruntime/exi.h"
#include "gxruntime/interrupts.h"
#include "gxruntime/si.h"

typedef struct WwHost {
    CPUState cpu;
    DolGuestMemory memory;
    DolGxRecompState gx;
    WwGxCheckpoint frontier;
    BluewakeCycleDomain cycles;
    DolInterrupts interrupts;
    DolDi di;
    DolExi exi;
    DolSiDevice si;
    const StaticRecompModuleDesc* module;
    const char* error;
    u32 error_address;
    u32 error_pc;
    u32 clock_remainder;
    u32 decrementer;
    u32 hid0;
    u64 alias_bytes;
    u64 backing_bytes;
    u32 alias_count;
    u64 edges;
    bool edge_stopped;
    bool decrementer_valid;
} WwHost;

bool ww_host_init(WwHost* host);
void ww_host_shutdown(WwHost* host);
bool ww_host_load(WwHost* host, const StaticRecompModuleDesc* module,
                   const BlueWakeRelData* data, u32 count, const char* dol);
bool ww_host_edge(void* user, CPUState* cpu, u32 address);
void ww_host_fail(WwHost* host, const char* error, u32 address, u32 pc);

#endif
