// P-A follow-up: the smallest Vita boot path to milestone M0's check, without the PC harness.
//
// pc_harness.cpp needs Aurora's window, the disc (aurora::dvd) and the whole run harness, and the
// Vita source build has none of them (AURORA_ENABLE_DVD=OFF). M0 needs less than the harness, so
// this main brings up only what the check and its milestone line read:
//
// 1. Aurora's OS: OSInit, with a 24 MiB MEM1 arena and 16 MiB ARAM set on AuroraConfig, as
//    native/sdk/tests/sdk_devices.cpp does. The VitaSDK newlib heap is 128 MiB by default, so the
//    PC harness's 256 MiB MEM1 calloc fails here and leaves no arena; staticInitCheck needs only
//    an arena. OSInit does not depend on aurora_initialize, and this path does not call it: the
//    AURORA_ENABLE_GX=OFF Vita build has no renderer or window, and M0 needs neither.
// 2. VI: VIInit and one VIWaitForRetrace (cos_sdk's VI), so VIGetRetraceCount - the one VI value
//    pc_milestone.cpp reads - is up.
// 3. pc::staticInitCheck, the M0 check split out of pc_smoke.cpp by the previous commit.
// 4. fpcMtd_Method, to touch the callable f_pc chain the profiles point at.
// 5. On the Vita, M1: two CPU-cleared 960x544 frames through sceDisplay (the first visible frame,
//    no GXM/Aurora renderer yet), then the "[cos] MILESTONE aurora-up" line and exit (0).
//    The host boot still stops at static-init: it has no Vita display.
//
// What this does not do: fill the list. It is native/vita/vita_profile_list.cpp, the bounded actor
// slice of cmake/vita_profile_slice.cmake: the slice's real g_profile_* identities are linked (its
// code and method tables are not), and the rest of the 502 definitions are still the REL actor
// units of the compile sweep (the remaining P-A blocker). The check reports 72 of 502; the
// milestone line says the same, so it is the M0 line, not a 502/502 claim. The boot path fails
// loudly (exit 1, a [cos] vita-boot: FAIL line) if OSInit leaves no MEM1 arena, if the first
// retrace does not advance the count, if the check reports an error, or if the chain touch fails.
#include "pc/pc_harness.h"
#include "pc/pc_static_init.h"
#include "pc_milestone_support.h"

#include "f_pc/f_pc_method.h"

#if defined(__vita__)
#include "vita_display_vita.h"
#endif

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

// The Vita boot only needs an arena, not the PC game's 256 MiB MEM1: a 256 MiB calloc exceeds the
// VitaSDK newlib heap (128 MiB by default) and OSInit then leaves OSGetArenaLo() null.
constexpr uint32_t kMem1Size = 24u * 1024 * 1024;
constexpr uint32_t kMem2Size = 16u * 1024 * 1024; // ARAM

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

    // Touch the callable f_pc chain the profiles' sub_method points at: fpcMtd_Method is the leaf
    // every g_fpcLf_Method call reaches; a NULL method is success, so the bounded path is safe.
    if (fpcMtd_Method(nullptr, nullptr) != TRUE) {
        return fail("fpcMtd_Method(nullptr, nullptr) did not return TRUE");
    }
    std::fprintf(stderr, "[cos] vita-boot: OS and VI up, static-init 0 errors, fpcMtd_Method ok\n");

#if defined(__vita__)
    // M0 must remain visible in the log; it no longer exits, because M1 follows.
    vitaMilestoneLogStaticInit();
    if (!vitaDisplayBringUp()) {
        return fail("the VI display clear/swap failed");
    }
    std::fprintf(stderr, "[cos] vita-boot: display up, 2 frames, hold 30 vblanks\n");
    vitaMilestoneAuroraUp();
#else
    // The M0 line and its COS_MILESTONE exit, printed by the harness's pc_milestone.cpp.
    vitaMilestoneStaticInit();
#endif
    return fail("pc_milestone returned without exiting");
}
