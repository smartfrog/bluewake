#include "native_ctx.h"

#include <assert.h>
#include <string.h>

static unsigned char output[8192];
static unsigned writes, mode, targets;

static int sink(void* user, uint32_t offset, const void* bytes, uint32_t size) {
    (void)user;
    ++writes;
    assert(offset + size <= sizeof(output));
    if (mode == 1u || (mode == 3u && writes == 2u) || (mode == 4u && writes == 3u)) {
        memcpy(output + offset, bytes, size - 1u);
        return (int)size - 1;
    }
    if (mode == 2u)
        return -1;
    memcpy(output + offset, bytes, size);
    return (int)size;
}

static void entry(uint32_t ctx, uint32_t target) {
    memset(&ww_abi_active, 0, sizeof(ww_abi_active));
    ww_abi_active.target = target;
    ww_abi_active.before.r0 = ctx;
    ww_abi_active.before.sp = 0x1000u;
    ww_abi_active.before.lr = 0x2001u;
    for (unsigned i = 0; i < 8; ++i) {
        ww_abi_active.before.r[i] = 0x1234u + i;
        ww_abi_active.before.d[i] = 0x1111111122222222ull + i;
    }
}

static bool invoke(void) {
    if (!ww_abi_begin())
        return false;
    ++targets;
    ww_abi_active.after = ww_abi_active.before;
    ww_abi_active.after.r0 = 1u;
    return true;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    ww_abi_init(0x1234u, sink, NULL);
    if (strcmp(argv[1], "unarmed") == 0) {
        assert(ww_abi_state == 0u && writes == 0u);
        ww_abi_arm(400u, 0x8000u);
        assert(ww_abi_state == 0u);
        return 0;
    }
    const bool thumb = strcmp(argv[1], "thumb") == 0;
    ww_abi_arm(168u, thumb ? 0x8001u : 0x8000u);
    entry(0x1234u, thumb ? 0x8001u : 0x8000u);
    if (strcmp(argv[1], "history-first") == 0 || strcmp(argv[1], "history-wrap") == 0) {
        const unsigned rounds = strcmp(argv[1], "history-first") == 0 ? 1u : 35u;
        memset(ww_abi_dump.pairs, 0xa5, sizeof(ww_abi_dump.pairs));
        for (unsigned round = 0; round < rounds; ++round) {
            unsigned char* bytes = (unsigned char*)&ww_abi_active;
            for (unsigned i = 0; i < sizeof(ww_abi_active); ++i)
                bytes[i] = (unsigned char)(i * 37u + round * 19u);
            ww_abi_active.target = 0x8000u;
            ww_abi_active.before.r0 = 0x1234u;
            ww_abi_active.before.sp = 0x1000u;
            assert(invoke());
            WwAbiPair pair = ww_abi_active;
            pair.returned = 1u;
            WwAbiDump expected = ww_abi_dump;
            memcpy(&expected.pairs[expected.next], &pair, sizeof(pair));
            expected.next = (expected.next + 1u) % 16u;
            if (expected.count < 16u)
                ++expected.count;
            assert(ww_abi_finish());
            assert(memcmp(&ww_abi_dump, &expected, sizeof(expected)) == 0);
            assert(memcmp(&ww_abi_active, &pair, sizeof(pair)) == 0);
            assert(ww_abi_state == 1u && writes == 0u);
        }
        return 0;
    } else if (strcmp(argv[1], "ctx-one") == 0) {
        ww_abi_active.before.r0 = 1u;
        assert(!invoke() && targets == 0u);
        assert(ww_abi_dump.failure == WW_ABI_CONTEXT);
    } else if (strcmp(argv[1], "target") == 0) {
        ww_abi_active.target = 0x8002u;
        assert(!invoke() && targets == 0u);
        assert(ww_abi_dump.failure == WW_ABI_TARGET);
    } else if (strcmp(argv[1], "overflow") == 0) {
        ww_abi_dump.invocations = UINT64_MAX;
        assert(!invoke() && targets == 0u);
        assert(ww_abi_dump.failure == WW_ABI_OVERFLOW);
    } else if (strcmp(argv[1], "entry-sp") == 0) {
        ww_abi_active.before.sp += 4u;
        assert(!invoke() && targets == 0u);
        assert(ww_abi_dump.failure == WW_ABI_SP);
    } else if (strcmp(argv[1], "ring") == 0) {
        for (unsigned i = 0; i < 35; ++i) {
            entry(0x1234u, 0x8000u);
            assert(invoke() && ww_abi_finish());
        }
        entry(1u, 0x8000u);
        assert(!invoke());
        assert(ww_abi_dump.count == 16u && ww_abi_dump.invocations == 36u);
        for (unsigned i = 0; i < 16; ++i) {
            const WwAbiPair* pair = &ww_abi_dump.pairs[(ww_abi_dump.next + i) % 16u];
            assert(pair->invocation == 20u + i);
            assert(pair->before.r0 == 0x1234u && pair->after.r0 == 1u);
        }
        WwAbiDump frozen = ww_abi_dump;
        WwAbiPair first = ww_abi_active;
        assert(!ww_abi_begin() && !ww_abi_finish());
        assert(memcmp(&frozen, &ww_abi_dump, sizeof(frozen)) == 0);
        assert(memcmp(&first, &ww_abi_active, sizeof(first)) == 0);
    } else {
        if (strcmp(argv[1], "short-write") == 0) mode = 1u;
        if (strcmp(argv[1], "failed-write") == 0) mode = 2u;
        if (strcmp(argv[1], "short-active") == 0) mode = 3u;
        if (strcmp(argv[1], "short-complete") == 0) mode = 4u;
        assert(invoke());
        if (strcmp(argv[1], "reentry") == 0)
            ww_abi_state = 4u;
        if (strcmp(argv[1], "register") == 0 || mode != 0u)
            ww_abi_active.after.r[0] = 1u;
        if (strcmp(argv[1], "sp") == 0)
            ww_abi_active.after.sp += 72u;
        if (strcmp(argv[1], "vfp") == 0)
            ww_abi_active.after.d[7] ^= 1u;
        const bool valid = strcmp(argv[1], "result-one") == 0 || thumb;
        assert(ww_abi_finish() == valid);
        assert(ww_abi_dump.failure == (valid ? 0u : strcmp(argv[1], "sp") == 0 ? WW_ABI_SP :
               strcmp(argv[1], "vfp") == 0 ? WW_ABI_VFP :
               strcmp(argv[1], "reentry") == 0 ? WW_ABI_REENTRY : WW_ABI_REGISTER));
        if (valid) {
            assert(writes == 0u && ww_abi_dump.count == 1u);
            return 0;
        }
    }
    assert(ww_abi_state == 2u);
    assert(writes == (mode == 1u || mode == 2u ? 1u : mode == 3u ? 2u : 3u));
    assert(ww_abi_dump.complete == (mode ? 0u : WW_ABI_COMPLETE));
    assert(ww_abi_dump.io_error == (mode ? 1u : 0u));
    uint32_t persisted_complete;
    memcpy(&persisted_complete, output + offsetof(WwAbiDump, complete), sizeof(persisted_complete));
    assert((persisted_complete == WW_ABI_COMPLETE) == (mode == 0u));
    assert(sizeof(ww_abi_dump) + sizeof(ww_abi_active) + 512u <= 8192u);
    return 0;
}
