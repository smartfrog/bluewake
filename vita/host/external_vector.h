#ifndef WW05_EXTERNAL_VECTOR_H
#define WW05_EXTERNAL_VECTOR_H

#include "core/cpu.h"

#define WW05_EXTERNAL_VECTOR 0x500u
#define WW05_EXTERNAL_VECTOR_BYTES 152u
#define WW05_FP_VECTOR PPC_VECTOR_FP_UNAVAILABLE

extern const u8 ww05_external_vector_reference[WW05_EXTERNAL_VECTOR_BYTES];
void func_00000500(CPUState* cpu);
extern const u8 ww05_fp_vector_reference[WW05_EXTERNAL_VECTOR_BYTES];
void func_00000800(CPUState* cpu);

#endif
