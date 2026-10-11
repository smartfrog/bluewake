#ifndef WW05_BAT_CONTRACT_H
#define WW05_BAT_CONTRACT_H

#include "core/cpu.h"

typedef struct WwBatContract {
    u32 regs[16];
} WwBatContract;

void ww_bat_init(WwBatContract* bat);
bool ww_bat_contains(u16 spr);
bool ww_bat_read(const WwBatContract* bat, u16 spr, u32* value);
const char* ww_bat_write(WwBatContract* bat, u16 spr, u32 value, u32 msr);
bool ww_bat_memory_supported(const WwBatContract* bat, u32 msr);

#endif
