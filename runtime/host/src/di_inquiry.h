#ifndef BLUEWAKE_DI_INQUIRY_H
#define BLUEWAKE_DI_INQUIRY_H

#include "core/cpu.h"

static inline void bluewake_di_inquiry_write(CPUState* cpu, u32 guest_address) {
    mem_write32(cpu, guest_address, 0x00000002u);
    mem_write32(cpu, guest_address + 4u, 0x20060526u);
    mem_write32(cpu, guest_address + 8u, 0x41000000u);
}

#endif
