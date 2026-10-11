#include "gx_checkpoint.h"
#include <assert.h>
#include <string.h>

static DolGxRecompState gx;

int main(int argc, char** argv) {
    assert(argc == 2);
    WwGxCheckpoint checkpoint = {0};
    dol_gx_recomp_init(&gx, NULL);
    if (strcmp(argv[1], "fragment") == 0) {
        assert(ww_gx_write(&checkpoint, &gx, 0x61u, 1u, 0x80001000u, 0x817FFF00u));
        assert(!checkpoint.complete);
        assert(gx.fifo.size == 0u);
        assert(ww_gx_write(&checkpoint, &gx, 0xFEFFFFFFu, 4u, 0x80001004u, 0x817FFF00u));
        assert(checkpoint.complete);
        assert(checkpoint.size == 5u);
        assert(checkpoint.pc == 0x80001000u);
        assert(gx.fifo.size == 5u);
        assert(gx.bp_valid[0xFE]);
    } else if (strcmp(argv[1], "unknown") == 0) {
        assert(!ww_gx_write(&checkpoint, &gx, 0xFFu, 1u, 1u, 2u));
        assert(checkpoint.error != NULL);
        assert(!checkpoint.complete);
        assert(gx.fifo.size == 0u);
    } else if (strcmp(argv[1], "suffix") == 0) {
        assert(ww_gx_write(&checkpoint, &gx, 0x0061FEFFFFFF0000ull, 8u, 1u, 2u));
        assert(checkpoint.complete);
        assert(checkpoint.bytes[0] == 0u);
        assert(gx.fifo.size == 1u);
        assert(checkpoint.trailing_bytes == 7u);
        assert(ww_gx_write(&checkpoint, &gx, 0xFFFFFFFFu, 4u, 3u, 4u));
        assert(checkpoint.trailing_bytes == 11u);
        assert(checkpoint.pc == 1u);
        assert(gx.fifo.size == 1u);
    } else if (strcmp(argv[1], "xf-count") == 0) {
        assert(ww_gx_write(&checkpoint, &gx, 0x10u, 1u, 1u, 2u));
        assert(!ww_gx_write(&checkpoint, &gx, 0x00FF1000u, 4u, 1u, 2u));
        assert(!checkpoint.complete);
        assert(gx.fifo.size == 0u);
    } else if (strcmp(argv[1], "cp") == 0) {
        assert(ww_gx_write(&checkpoint, &gx, 0x0830u, 2u, 1u, 2u));
        assert(gx.fifo.size == 0u);
        assert(ww_gx_write(&checkpoint, &gx, 0x12345678u, 4u, 3u, 2u));
        assert(checkpoint.complete);
        assert(gx.matrix_index_a == 0x12345678u);
    } else if (strcmp(argv[1], "parser-error") == 0) {
        assert(ww_gx_write(&checkpoint, &gx, 0x20u, 1u, 1u, 2u));
        assert(!ww_gx_write(&checkpoint, &gx, 0u, 4u, 3u, 2u));
        assert(!checkpoint.complete);
        assert(checkpoint.error != NULL);
    } else {
        return 2;
    }
    return 0;
}
