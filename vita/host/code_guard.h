#ifndef WW05_CODE_GUARD_H
#define WW05_CODE_GUARD_H

#include "gxruntime/loader.h"

typedef struct {
    DolLayout layout;
    u8* text[DOL_NUM_TEXT];
    u64 bytes;
} WwCodeGuard;

bool ww_code_guard_init(WwCodeGuard* guard, const CPUState* cpu, const DolLayout* layout);
bool ww_code_guard_matches(const WwCodeGuard* guard, const CPUState* cpu, u32 address);
void ww_code_guard_free(WwCodeGuard* guard);

#endif
