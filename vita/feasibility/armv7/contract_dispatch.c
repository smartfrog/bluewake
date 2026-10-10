#include "generated_composite.h"
#include "StaticRecompABI.h"
#include "direct_calls.h"

unsigned dolrecomp_call_depth;
BwChunkFn* const bw_chunk_fns = s_dolrecomp_chunk_fns;

static DolRecompFunction bluewake_mod_extra_find(u32 address) {
    (void)address;
    return NULL;
}

BwChunkFn bw_find_chunk(u32 address) {
    return dolrecomp_find_original(address);
}

int bw_native_call(CPUState* cpu, u32 address) {
    (void)cpu;
    (void)address;
    return 0;
}

int fixture_dispatch(CPUState* cpu) {
    return dolrecomp_call(cpu, cpu->pc);
}
