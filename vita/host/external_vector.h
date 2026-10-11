#ifndef WW05_EXTERNAL_VECTOR_H
#define WW05_EXTERNAL_VECTOR_H

#include "core/cpu.h"

#define WW05_EXTERNAL_VECTOR 0x500u
#define WW05_EXTERNAL_VECTOR_BYTES 152u

extern const u8 ww05_external_vector_reference[WW05_EXTERNAL_VECTOR_BYTES];
void func_00000500(CPUState* cpu);

#endif
