#include "fixture.h"
#include <stdio.h>

_Static_assert(sizeof(((CPUState*)0)->ram) == sizeof(void*), "RAM must be a host pointer");
_Static_assert(sizeof(((CPUState*)0)->external_pointer) == sizeof(void*), "callback pointer width");
_Static_assert(sizeof(((CPUState*)0)->pc) == 4, "guest PC must remain 32-bit");
#ifdef __vita__
_Static_assert(sizeof(void*) == 4, "Vita requires ILP32");
#endif

const FixtureAbi fixture_abi __attribute__((section(".fixture_abi"), used)) = {
    sizeof(void*), sizeof(CPUState), GXRUNTIME_CPU_ABI_VERSION,
    offsetof(CPUState, ram), offsetof(CPUState, external_pointer),
};

void fixture_print_abi(void) {
    printf("ABI pointer_bytes=%u cpu_bytes=%u cpu_abi=%u ram_offset=%u external_pointer_offset=%u\n",
           fixture_abi.pointer_bytes, fixture_abi.cpu_bytes, fixture_abi.cpu_abi,
           fixture_abi.ram_pointer_offset, fixture_abi.external_pointer_offset);
}
