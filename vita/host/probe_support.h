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
        {"cp-fifo-base-high", 0xCC000022u, 0x80320FD8u, 0x42u, 0x03FFu, 2u},
        {"cp-fifo-end-low", 0xCC000024u, 0x80320F8Cu, 0x9F5Cu, 0xFFE0u, 2u},
        {"cp-fifo-end-high", 0xCC000026u, 0x80320FE8u, 0x4Cu, 0x03FFu, 2u},
        {"cp-fifo-wptr-low", 0xCC000034u, 0x80320FA4u, 0x9F60u, 0xFFE0u, 2u},
        {"cp-fifo-wptr-high", 0xCC000036u, 0x80321008u, 0x42u, 0x03FFu, 2u},
        {"cp-fifo-rptr-low", 0xCC000038u, 0x80320FB0u, 0x9F60u, 0xFFE0u, 2u},
        {"cp-fifo-rptr-high", 0xCC00003Au, 0x80321018u, 0x42u, 0x03FFu, 2u},
        {"cp-fifo-high-watermark-low", 0xCC000028u, 0x80320FBCu, 0xC000u, 0xFFE0u, 2u},
        {"cp-fifo-high-watermark-high", 0xCC00002Au, 0x80321028u, 9u, 0x03FFu, 2u},
        {"cp-fifo-low-watermark-low", 0xCC00002Cu, 0x80320FC8u, 0u, 0xFFE0u, 2u},
        {"cp-fifo-low-watermark-high", 0xCC00002Eu, 0x80321038u, 5u, 0x03FFu, 2u},
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

static inline void ww_probe_fifo_setup(WwHost* host, bool linked) {
    host->pi_fifo_base = 0x1000u;
    host->pi_fifo_end = 0x1060u;
    host->pi_fifo_write_pointer = 0x1000u;
    host->cp_fifo_base = 0x1000u;
    host->cp_fifo_end = 0x1060u;
    host->cp_fifo_write_pointer = 0x1000u;
    host->cp_fifo_read_pointer = 0x1000u;
    host->cpu.external_write(&host->cpu, 0xCC000002u, linked ? 0x14u : 4u, 2u);
    assert(host->error == NULL);
    memset(host->cpu.ram + 0xFE0u, 0xA5u, 192u);
}

static inline void ww_probe_gather_bytes(WwHost* host, const u8* bytes, u32 start, u32 end, u8 width) {
    for (u32 offset = start; offset < end; offset += width) {
        u64 value = 0u;
        for (u8 index = 0u; index < width; ++index)
            value = (value << 8u) | bytes[offset + index];
        host->cpu.pc = 0x80001000u + (offset / width) * 4u;
        host->cpu.gpr[1] = 0x817FFF00u - offset;
        switch (width) {
        case 1u: mem_write8(&host->cpu, 0xCC008000u, (u8)value); break;
        case 2u: mem_write16(&host->cpu, 0xCC008000u, (u16)value); break;
        case 4u: mem_write32(&host->cpu, 0xCC008000u, (u32)value); break;
        case 8u: mem_write64(&host->cpu, 0xCC008000u, value); break;
        default: assert(false);
        }
        if (host->error != NULL)
            break;
    }
}

#endif
