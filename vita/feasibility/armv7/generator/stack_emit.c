#include "backend/emitter.h"
#include "frontend/decoder.h"

int main(void) {
    const u32 words[] = {
        0x9421FFF0u,
        0x7C0802A6u,
        0x90010014u,
        0x2C030000u,
        0x41820010u,
        0x3863FFFFu,
        0x7D8903A6u,
        0x4E800421u,
        0x80010014u,
        0x7C0803A6u,
        0x38210010u,
        0x4E800020u,
    };
    PPCInst instructions[sizeof words / sizeof words[0]];
    for (u32 i = 0; i < sizeof words / sizeof words[0]; ++i)
        instructions[i] = ppc_decode(words[i], 0x80006000u + i * 4u);
    return !emit_function(stdout, instructions, sizeof words / sizeof words[0], 0x80006000u);
}
