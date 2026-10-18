// ILP32 runtime/ABI smoke for the Vita source port.
//
// The compile-time profile is check/ilp32_check.cpp (linked into the Vita target as an OBJECT
// library). This unit exercises the same assumptions at run time and, where the host has an
// equivalent, runs on the host too:
//   - pointer/long/size_t/sizeof and max alignment, and a mixed pointer/long/double layout;
//   - std::atomic<uint32_t> size/lock-freedom and operations;
//   - malloc/calloc/free, zeroing and allocator alignment;
//   - memcpy/memmove/memset including unaligned offsets, plus the BE/BSWAP helpers;
//   - the boot path (Aurora OSInit, VIInit, one retrace), the same sequence as cos_vita_boot.
//
// On a 64-bit host the size expectations are the LP64 ones; the ILP32 sizes and the engine's
// ILP32 offsets are compile-time errors in cos_ilp32_check.cpp. Running the ARM binary itself
// needs Vita3K or hardware (not started here).
#include "pc/cos_pointer_width.h"

#include "helpers/endian.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <aurora/aurora.h>
#include <dolphin/os.h>
#include <dolphin/vi.h>

#if UINTPTR_MAX == 0xffffffffu
static_assert(sizeof(void*) == 4, "ILP32: sizeof(void*)");
static_assert(sizeof(long) == 4, "ILP32: sizeof(long)");
static_assert(sizeof(size_t) == 4, "ILP32: sizeof(size_t)");
static_assert(sizeof(uintptr_t) == 4, "ILP32: sizeof(uintptr_t)");
static_assert(sizeof(intptr_t) == 4, "ILP32: sizeof(intptr_t)");
#else
static_assert(sizeof(void*) == 8, "LP64: sizeof(void*)");
static_assert(sizeof(long) == 8, "LP64: sizeof(long)");
static_assert(sizeof(size_t) == 8, "LP64: sizeof(size_t)");
#endif

static_assert(sizeof(std::atomic<uint32_t>) == sizeof(uint32_t),
              "atomic<uint32_t> must stay 4 bytes");
static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "atomic<uint32_t> must be lock-free");

namespace {

struct Mixed {
    char c;
    void* p;
    long l;
    size_t s;
    double d;
};

#if UINTPTR_MAX == 0xffffffffu
static_assert(offsetof(Mixed, c) == 0, "mixed: c");
static_assert(offsetof(Mixed, p) == 4, "mixed: p");
static_assert(offsetof(Mixed, l) == 8, "mixed: l");
static_assert(offsetof(Mixed, s) == 12, "mixed: s");
static_assert(offsetof(Mixed, d) == 16, "mixed: d");
static_assert(sizeof(Mixed) == 24, "mixed: size");
#else
static_assert(offsetof(Mixed, p) == 8, "mixed: p");
static_assert(offsetof(Mixed, l) == 16, "mixed: l");
static_assert(offsetof(Mixed, s) == 24, "mixed: s");
static_assert(offsetof(Mixed, d) == 32, "mixed: d");
static_assert(sizeof(Mixed) == 40, "mixed: size");
#endif

int sFailures;

void check(bool ok, const char* what) {
    std::printf("ilp32-runtime: %-42s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        sFailures++;
    }
}

} // namespace

namespace aurora {
extern AuroraConfig g_config;
} // namespace aurora

int main() {
    std::printf("ilp32-runtime: pointer=%u long=%u size_t=%u max_align=%u\n",
                (unsigned)sizeof(void*), (unsigned)sizeof(long), (unsigned)sizeof(size_t),
                (unsigned)alignof(std::max_align_t));
    check(sizeof(void*) == sizeof(uintptr_t), "pointer fits uintptr_t");
    check(alignof(std::max_align_t) >= alignof(void*), "max_align_t covers a pointer");
    check(sizeof(Mixed) == offsetof(Mixed, d) + sizeof(double), "mixed layout");

    std::atomic<uint32_t> counter{0};
    check(sizeof(counter) == sizeof(uint32_t), "atomic<uint32_t> is 4 bytes");
    check(counter.is_always_lock_free, "atomic<uint32_t> is lock-free");
    uint32_t expected = 0;
    const bool exchanged = counter.compare_exchange_strong(expected, 1);
    counter.fetch_add(1);
    check(exchanged && counter.load() == 2, "atomic compare_exchange/fetch_add");

    void* block = std::malloc(64);
    check(block != nullptr, "malloc(64)");
    if (block != nullptr) {
        check((reinterpret_cast<uintptr_t>(block) % alignof(std::max_align_t)) == 0,
              "malloc alignment");
        std::memset(block, 0xA5, 64);
        std::free(block);
    }

    unsigned char* zeroed = static_cast<unsigned char*>(std::calloc(8, 8));
    check(zeroed != nullptr, "calloc(8, 8)");
    if (zeroed != nullptr) {
        bool allZero = true;
        for (int i = 0; i < 64; i++) {
            allZero = allZero && zeroed[i] == 0;
        }
        check(allZero, "calloc zeroes its block");
        std::free(zeroed);
    }

    unsigned char source[32];
    unsigned char target[32];
    for (int i = 0; i < 32; i++) {
        source[i] = static_cast<unsigned char>(i * 7 + 3);
    }
    std::memset(target, 0, sizeof target);
    std::memcpy(target + 1, source + 3, 16);
    bool copyOk = true;
    for (int i = 0; i < 16; i++) {
        copyOk = copyOk && target[1 + i] == static_cast<unsigned char>((3 + i) * 7 + 3);
    }
    check(copyOk, "memcpy at unaligned offsets");

    unsigned char beforeMove[16];
    std::memcpy(beforeMove, target, sizeof beforeMove);
    std::memmove(target + 4, target, 16);
    bool moveOk = true;
    for (int i = 0; i < 16; i++) {
        moveOk = moveOk && target[4 + i] == beforeMove[i];
    }
    check(moveOk, "memmove overlap");

    check(BSWAP64(0x0102030405060708ULL) == 0x0807060504030201ULL, "BSWAP64");
    check(be64(0x0102030405060708ULL) == 0x0807060504030201ULL, "be64 disc order");

    aurora::g_config.mem1Size = 24u * 1024 * 1024;
    aurora::g_config.mem2Size = 16u * 1024 * 1024;
    OSInit();
    VIInit();
    const u32 before = VIGetRetraceCount();
    VIWaitForRetrace();
    const u32 after = VIGetRetraceCount();
    check(after == before + 1, "Aurora OSInit + VI retrace");

    if (sFailures != 0) {
        std::printf("ilp32-runtime: %d failure(s)\n", sFailures);
        return 1;
    }
    std::printf("ilp32-runtime: ok\n");
    return 0;
}
