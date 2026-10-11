#ifndef WW05_PROBE_SUPPORT_H
#define WW05_PROBE_SUPPORT_H

#include "boot_host.h"
#include <assert.h>
#include <string.h>

typedef struct WwFifoProbeRegister {
    const char* name;
    u32 address;
    u32 pc;
    u32 value;
    u32 mask;
    u8 width;
} WwFifoProbeRegister;

static inline const WwFifoProbeRegister* ww_probe_fifo_register(const char* name, const char** scenario) {
    static const WwFifoProbeRegister registers[] = {
        {"pi-fifo-base", 0xCC00300Cu, 0x80320EF8u, 0x00429F60u, 0xFFFFFFE0u, 4u},
        {"pi-fifo-end", 0xCC003010u, 0x80320F08u, 0x004C9F5Cu, 0xFFFFFFE0u, 4u},
        {"pi-fifo-wptr", 0xCC003014u, 0x80320F1Cu, 0x00429F60u, 0xFFFFFFE0u, 4u},
        {"cp-fifo-base-low", 0xCC000020u, 0x80320F80u, 0x9F60u, 0xFFE0u, 2u},
        {"cp-fifo-end-low", 0xCC000024u, 0x80320F8Cu, 0x9F5Cu, 0xFFE0u, 2u},
        {"cp-fifo-wptr-low", 0xCC000034u, 0x80320FA4u, 0x9F60u, 0xFFE0u, 2u},
        {"cp-fifo-rptr-low", 0xCC000038u, 0x80320FB0u, 0x9F60u, 0xFFE0u, 2u},
        {"cp-fifo-high-watermark-low", 0xCC000028u, 0x80320FBCu, 0xC000u, 0xFFE0u, 2u},
        {"cp-fifo-low-watermark-low", 0xCC00002Cu, 0x80320FC8u, 0u, 0xFFE0u, 2u},
    };
    for (size_t index = 0u; index < sizeof(registers) / sizeof(registers[0]); ++index) {
        const size_t length = strlen(registers[index].name);
        if (strncmp(name, registers[index].name, length) == 0 && (name[length] == '-' || name[length] == '\0')) {
            *scenario = name + length + (name[length] == '-' ? 1u : 0u);
            return &registers[index];
        }
    }
    *scenario = NULL;
    return NULL;
}

static inline void charge_cycles(WwHost* host, u64 cycles) {
    assert(cycles <= INT64_MAX);
    bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
    bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
    host->cpu.downcount = -(s64)cycles;
    bluewake_cycle_domain_end_turn(&host->cycles, &host->cpu);
}

#endif
