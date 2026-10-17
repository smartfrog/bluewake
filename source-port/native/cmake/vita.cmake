# E1 (PLAN_C3.md §9): package the headless source skeleton for the PS Vita.
#
# Build only: this produces the VPK the E2 runs (decided separately) will install. It reuses the
# VitaSDK tooling exactly as vita/feasibility/gx does (vita_create_self + vita_create_vpk), never a
# second packaging path.
#
# Enabled only for the Vita cross build: 32-bit (ILP32) target with the VitaSDK toolchain, which
# sets CMAKE_SYSTEM_NAME to Generic. The host builds never include this file's targets.
include_guard(GLOBAL)

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 4 OR NOT CMAKE_SYSTEM_NAME STREQUAL "Generic")
    return()
endif ()
if (NOT DEFINED ENV{VITASDK})
    message(WARNING "cos_native: VITASDK is not set; the Vita package targets are skipped")
    return()
endif ()
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

# The E1 binary: cos_sdk_smoke, the headless skeleton smoke (no window, no GPU; it prints "ok"
# and exits 0 when every SDK test passes). Runs are E2's.
if (TARGET cos_sdk_smoke)
    # vita-elf-create appends the SCE data at the end of segment 0 and fails when segment 1
    # starts too close: anchor the RW base with margin, as the WW-02 fixture does.
    target_link_options(cos_sdk_smoke PRIVATE "-Wl,--section-start=.init_array=0x81100000")
    vita_create_self(cos_sdk_smoke.self cos_sdk_smoke)
    vita_create_vpk(cos_e1_skeleton.vpk BWWG00031 cos_sdk_smoke.self VERSION 00.01
            NAME "BlueWake source skeleton (headless smoke)")
    message(STATUS "cos_native: Vita skeleton package targets: cos_sdk_smoke.self, cos_e1_skeleton.vpk")
else ()
    message(WARNING "cos_native: cos_sdk_smoke is not built; the Vita package targets are skipped")
endif ()
