#include "backend/emitter.h"
#include "frontend/decoder.h"

int main(void) {
    const struct { u32 address; u32 count; u32 words[5]; } routines[] = {
        {0x80007000u, 2, {0xFC22182Au, 0x4E800020u}},
        {0x80007020u, 2, {0xFC201018u, 0x4E800020u}},
        {0x80007040u, 2, {0xFC2200F2u, 0x4E800020u}},
        {0x80007060u, 2, {0xEC2200F2u, 0x4E800020u}},
        {0x80007080u, 2, {0xF0448000u, 0x4E800020u}},
        {0x80007100u, 4, {0x7D6802A6u, 0x4BFFFEFDu, 0x7D6803A6u, 0x4E800020u}},
        {0x80007120u, 5, {0x7D6802A6u, 0x7D8903A6u, 0x4E800421u, 0x7D6803A6u, 0x4E800020u}},
    };
    for (unsigned r = 0; r < sizeof routines / sizeof routines[0]; ++r) {
        PPCInst instructions[5];
        for (u32 i = 0; i < routines[r].count; ++i) {
            instructions[i] = ppc_decode(routines[r].words[i], routines[r].address + i * 4u);
            if (instructions[i].op == PPC_OP_UNKNOWN)
                return 1;
        }
        if (!emit_function(stdout, instructions, routines[r].count, routines[r].address))
            return 1;
    }
    return fflush(stdout) != 0 || ferror(stdout);
}
