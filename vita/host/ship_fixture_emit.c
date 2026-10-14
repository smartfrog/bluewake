#include "backend/emitter.h"
#include "frontend/decoder.h"
#include <stdlib.h>

static int read_word(FILE* input, u32* value) {
    u8 bytes[4];
    const size_t size = fread(bytes, 1u, 4u, input);
    if (size != 4u)
        return size == 0u && !ferror(input) ? 0 : -1;
    *value = ((u32)bytes[0] << 24u) | ((u32)bytes[1] << 16u) | ((u32)bytes[2] << 8u) | bytes[3];
    return 1;
}

int main(int argc, char** argv) {
    if (argc != 2)
        return 1;
    FILE* input = fopen(argv[1], "rb");
    if (input == NULL)
        return 1;
    emit_header_for_cpu(stdout, DOLRECOMP_CPU_GEKKO);
    u32 address, count;
    bool success = true;
    bool emitted = false;
    int record;
    while ((record = read_word(input, &address)) == 1) {
        if (read_word(input, &count) != 1 || count == 0u || count > 64u) {
            success = false;
            break;
        }
        PPCInst instructions[64];
        for (u32 at = 0u; at < count; ++at) {
            u32 word;
            if (read_word(input, &word) != 1) {
                success = false;
                break;
            }
            instructions[at] = ppc_decode(word, address + at * 4u);
        }
        if (!success || !emit_function(stdout, instructions, count, address)) {
            success = false;
            break;
        }
        emitted = true;
    }
    success &= record == 0 && emitted && !ferror(input);
    success &= fclose(input) == 0;
    emit_footer(stdout);
    return !success || fflush(stdout) != 0 || ferror(stdout);
}
