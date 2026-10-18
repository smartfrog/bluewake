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

# Link settings every Vita executable of this build needs: cos_sdk_smoke and cos_vita_boot.
function(cos_vita_configure_executable target)
    # The binary is bigger than the WW-02 fixture: anchor the RW base well above .text
    # (vita-elf-create appends the SCE data at the end of segment 0 and fails when segment 1
    # starts too close).
    target_link_options(${target} PRIVATE "-Wl,--section-start=.init_array=0x81400000")
    # libstdc++ references the pthread functions weakly (its __gthrw declarations): a weak
    # reference does not pull an archive member, the symbol stays undefined and GNU ld patches
    # the call to a nop. libstdc++ then reads the leftover register as an error code and throws
    # std::system_error("Unknown error") -- the E2-bis fault, where the first std::call_once
    # (Aurora's OSGetTime) threw on pthread_once. Force the PTE members these programs reach so
    # every call resolves; the host build links a full pthread and is untouched.
    target_link_options(${target} PRIVATE
            "-Wl,-u,pthread_once"
            "-Wl,-u,pthread_cond_wait"
            "-Wl,-u,pthread_cond_timedwait"
            "-Wl,-u,pthread_cond_signal"
            "-Wl,-u,pthread_cond_broadcast"
            "-Wl,-u,pthread_cond_destroy"
            "-Wl,-u,pthread_cancel"
            "-Wl,-u,pthread_rwlock_rdlock"
            "-Wl,-u,pthread_rwlock_unlock"
            "-Wl,-u,pthread_rwlock_wrlock")
    # Aurora's libraries reference each other in a cycle (core <-> vi): group them, as a static
    # link of the pair requires.
    target_link_libraries(${target} PRIVATE "-Wl,--start-group" ${COS_AURORA_LIBS} "-Wl,--end-group")
endfunction()

# The E1 binary: cos_sdk_smoke, the headless skeleton smoke (no window, no GPU; it prints "ok"
# and exits 0 when every SDK test passes). Runs are E2's.
if (TARGET cos_sdk_smoke)
    # E1 scope: the services the skeleton does not build yet are skipped loudly by the tests
    # (COS_SDK_SMOKE_NO_*), and two libc functions newlib declares but does not implement are
    # stubbed (native/vita/stubs_libc.cpp).
    target_compile_definitions(cos_sdk_smoke PRIVATE
            COS_SDK_SMOKE_NO_DVD=1    # no aurora::dvd on this target (host-only for now)
            COS_SDK_SMOKE_NO_GX=1     # no aurora_gx/aurora_gd (GX is E3/E4)
            COS_SDK_SMOKE_NO_CHILD=1  # no child processes on the Vita
            COS_SDK_SMOKE_NO_DSP=1    # no DSP HLE on this target (the stub makes DSPInit abort)
            COS_SDK_SMOKE_NO_AI=1)    # no AI engine: its detached DMA thread's stack faults in the
                                      # emulator (E2-quater); audio is deferred by decision
    target_sources(cos_sdk_smoke PRIVATE "${COS_NATIVE_ROOT}/vita/stubs_libc.cpp")
    cos_vita_configure_executable(cos_sdk_smoke)
    vita_create_self(cos_sdk_smoke.self cos_sdk_smoke)
    vita_create_vpk(cos_e1_skeleton.vpk BWWG00031 cos_sdk_smoke.self VERSION 00.01
            NAME "BlueWake source skeleton (headless smoke)")
    message(STATUS "cos_native: Vita skeleton package targets: cos_sdk_smoke.self, cos_e1_skeleton.vpk")
else ()
    message(WARNING "cos_native: cos_sdk_smoke is not built; the Vita package targets are skipped")
endif ()

# P-A follow-up: the smallest boot path to the M0 static-init check (native/vita/vita_boot.cpp).
# Aurora's OSInit and VI, then pc::staticInitCheck, with no pc_harness, no Aurora DVD and no
# renderer. The profile list is the bounded actor slice of native/vita/vita_profile_list.cpp
# (cmake/vita_profile_slice.cmake), reduced to its g_profile_* data. Not packaged: packaging and
# Vita3K runs are separate. The E1 build directory and flags build it as is:
#   cmake --build <e1-vita> --target cos_vita_boot
if (COS_VITA_PROFILE_SLICE_OBJECT AND COS_VITA_FPC_CHAIN_OBJECT)
    add_executable(cos_vita_boot
            "${COS_NATIVE_ROOT}/vita/vita_boot.cpp"
            "${COS_NATIVE_ROOT}/vita/vita_profile_list.cpp"
            "${COS_NATIVE_ROOT}/src/pc/harness/pc_static_init.cpp"
            "${COS_ROOT}/src/f_pc/f_pc_profile.cpp"
            "${COS_VITA_PROFILE_SLICE_OBJECT}"
            "${COS_VITA_FPC_CHAIN_OBJECT}"
            "${COS_VITA_DELETEHEAP_OBJECT}"
            $<TARGET_OBJECTS:cos_vita_milestone_units>)
    set_source_files_properties("${COS_VITA_PROFILE_SLICE_OBJECT}"
            "${COS_VITA_FPC_CHAIN_OBJECT}" "${COS_VITA_DELETEHEAP_OBJECT}" PROPERTIES
            EXTERNAL_OBJECT TRUE GENERATED TRUE)
    target_link_libraries(cos_vita_boot PRIVATE cos_game_headers cos_sdk)
    target_sources(cos_vita_boot PRIVATE "${COS_NATIVE_ROOT}/vita/stubs_libc.cpp")
    target_link_options(cos_vita_boot PRIVATE -Wl,--gc-sections)
    add_dependencies(cos_vita_boot cos_vita_profile_slice cos_vita_fpc_chain
            cos_vita_deleteheap cos_vita_milestone_units)
    cos_vita_configure_executable(cos_vita_boot)
else ()
    message(WARNING "cos_native: the actor profile slice is unavailable; cos_vita_boot is skipped")
endif ()

# ILP32 runtime/ABI test: the compile-time profile is cos_ilp32_check, the runtime checks are
# check/ilp32_runtime.cpp (sizes/atomics/heap/memcpy/boot smoke). Not packaged.
add_executable(cos_vita_ilp32_test EXCLUDE_FROM_ALL
        "${COS_NATIVE_ROOT}/check/ilp32_runtime.cpp"
        $<TARGET_OBJECTS:cos_ilp32_check>)
target_link_libraries(cos_vita_ilp32_test PRIVATE cos_game_headers cos_sdk)
target_sources(cos_vita_ilp32_test PRIVATE "${COS_NATIVE_ROOT}/vita/stubs_libc.cpp")
cos_vita_configure_executable(cos_vita_ilp32_test)
