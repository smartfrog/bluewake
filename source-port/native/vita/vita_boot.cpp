// P-A follow-up: the smallest Vita boot path to milestone M0's check, without the PC harness.
//
// pc_harness.cpp needs Aurora's window, the disc (aurora::dvd) and the whole run harness, and the
// Vita source build has none of them (AURORA_ENABLE_DVD=OFF). M0 needs less than the harness, so
// this main brings up only what the check and its milestone line read:
//
// 1. Aurora's OS: OSInit, with the game's memory map (MEM1 256 MiB, ARAM 16 MiB, decision H5) set
//    on AuroraConfig as native/sdk/tests/sdk_devices.cpp does. OSInit does not depend on
//    aurora_initialize, and this path does not call it: the AURORA_ENABLE_GX=OFF Vita build has no
//    renderer or window, and M0 needs neither.
// 2. VI: VIInit and one VIWaitForRetrace (cos_sdk's VI), so VIGetRetraceCount - the one VI value
//    pc_milestone.cpp reads - is up.
// 3. pc::staticInitCheck, the M0 check split out of pc_smoke.cpp by the previous commit.
//
// What this does not do: reach M0. The profile list is
// native/vita/vita_profile_list.cpp, a bounded stub with no actor unit in it, because the 502
// g_profile_* definitions are the REL actor units of the compile sweep (the remaining P-A
// blocker). The check therefore reports "0 of 502 profile slots filled". The boot path fails
// loudly (exit 1, a [cos] vita-boot: FAIL line) if OSInit leaves no MEM1 arena, if the first
// retrace does not advance the count, or if the check reports an error.
#include "pc/pc_static_init.h"

#include <aurora/aurora.h>
#include <dolphin/os.h>
#include <dolphin/vi.h>

#include <cstdint>
#include <cstdio>
#include <unistd.h>

namespace aurora {
// Normally filled by aurora_initialize (the PC harness); this path sets the two sizes itself and
// never calls it. Same access as native/sdk/tests/sdk_devices.cpp.
extern AuroraConfig g_config;
} // namespace aurora

namespace {

constexpr uint32_t kMem1Size = 256u * 1024 * 1024; // decision H5, as pc_aurora_init
constexpr uint32_t kMem2Size = 16u * 1024 * 1024;  // ARAM, as pc_aurora_init

int fail(const char* what) {
    std::fprintf(stderr, "[cos] vita-boot: FAIL %s\n", what);
    return 1;
}

} // namespace

int main() {
    aurora::g_config.mem1Size = kMem1Size;
    aurora::g_config.mem2Size = kMem2Size;

    OSInit();
    std::fprintf(stderr, "[cos] vita-boot: OSInit MEM1 %u MiB ARAM %u MiB\n", kMem1Size >> 20,
                 kMem2Size >> 20);
    if (OSGetArenaLo() == nullptr) {
        return fail("OSInit left no MEM1 arena (AuroraConfig.mem1Size)");
    }

    VIInit();
    VIWaitForRetrace();
    const u32 retraces = VIGetRetraceCount();
    if (retraces != 1) {
        return fail("VIWaitForRetrace did not advance VIGetRetraceCount");
    }
    std::fprintf(stderr, "[cos] vita-boot: VIInit, retrace %u\n", (unsigned int)retraces);

    const int errors = pc::staticInitCheck(STDERR_FILENO);
    if (errors != 0) {
        std::fprintf(stderr, "[cos] vita-boot: FAIL the static-init check reported %d error(s)\n",
                     errors);
        return 1;
    }
    std::fprintf(stderr,
                 "[cos] vita-boot: ok, OS and VI up and the static-init check ran without errors; "
                 "the profile list is the bounded stub, not the game's (the 502 g_profile_* "
                 "definitions are still the REL sweep)\n");
    return 0;
}
