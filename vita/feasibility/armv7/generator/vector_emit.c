#include "backend/emitter.h"
#include "frontend/decoder.h"
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3 ||
        (argc == 3 && strcmp(argv[2], "00000500") != 0 && strcmp(argv[2], "00000800") != 0)) {
        fprintf(stderr, "vector emitter requires one binary input and an observed 00000500 or 00000800 base\n");
        return 1;
    }
    const u32 vector = argc == 3 && strcmp(argv[2], "00000800") == 0 ? 0x800u : 0x500u;
    FILE* input = fopen(argv[1], "rb");
    if (input == NULL) {
        perror("external vector input");
        return 1;
    }
    u8 bytes[152];
    const bool complete = fread(bytes, 1u, sizeof(bytes), input) == sizeof(bytes) &&
                          fgetc(input) == EOF && !ferror(input);
    const int closed = fclose(input);
    if (!complete || closed != 0) {
        fprintf(stderr, "external vector input requires exactly 152 readable bytes\n");
        return 1;
    }
    PPCInst instructions[38];
    for (u32 i = 0u; i < 38u; ++i) {
        const u8* word = bytes + i * 4u;
        const u32 raw = ((u32)word[0] << 24u) | ((u32)word[1] << 16u) | ((u32)word[2] << 8u) | word[3];
        instructions[i] = ppc_decode(raw, vector + i * 4u);
    }
    fprintf(stdout, "#define DOLRECOMP_CPU_HEADER \"core/cpu.h\"\n");
    fprintf(stdout, "#define DOLRECOMP_C_LOOP_CYCLE_BUDGET (ctx->cycle_budget)\n");
    emit_header_for_cpu(stdout, DOLRECOMP_CPU_GEKKO);
    if (!emit_function(stdout, instructions, 38u, vector))
        return 1;
    fprintf(stdout, "const u8 ww05_%s_vector_reference[152] = {", vector == 0x500u ? "external" : "fp");
    for (u32 i = 0u; i < sizeof(bytes); ++i)
        fprintf(stdout, "%u,", bytes[i]);
    fprintf(stdout, "};\n");
    emit_footer(stdout);
    return fflush(stdout) != 0 || ferror(stdout);
}
