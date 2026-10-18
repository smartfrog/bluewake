# P-A blocker 2 POC (build-f5/source-port-next-pa/REPORT.md, blocker 2): a Vita static-init main that
# needs no PC harness and no Aurora DVD, and the link check that proves it.
#
# Blocker 2 was "pc_smoke.cpp/pc_harness pull Aurora DVD/Aurora symbols absent on Vita; needs a small
# Vita main or split static-init unit". Both halves are here:
# - src/pc/harness/pc_static_init.cpp holds the M0 check, out of pc_smoke.cpp, with no harness,
#   Aurora or disc dependency (pc_smoke.cpp calls the same function, so the PC harness output is
#   unchanged);
# - native/vita/vita_static_init.cpp is a main that runs it;
# - native/vita/stubs_pc_features.cpp and stubs_dvd.cpp answer the PC feature hooks and the four DVD
#   entry points the game units reference, so the covered units link with the PC features off.
#
# What this proves: the M0 check compiles for ARMv7 from a Vita main and its link needs no pc::
# harness symbol and no Aurora/GX/DVD/feature symbol from outside the stubs. What it does not: M0
# itself. "502 of 502 profile slots filled" needs the 502 REL actor units' g_profile_* definitions
# (the compile sweep). The "[cos] MILESTONE static-init" line is now reached through the real
# pc_milestone.cpp, with a minimal gConfig/support (vita/pc_milestone_support.cpp). The check
# prints the g_profile_* count as the size of what remains.
#
# 32-bit Vita builds only: a host build has the real features and Aurora DVD, so the stubs would
# duplicate them.
include_guard(GLOBAL)

# pc_milestone.cpp plus the smallest gConfig, writef, elapsedMs and pc_exit it needs on a path with
# no PC harness. EXCLUDE_FROM_ALL: only the boot executables depend on it.
add_library(cos_vita_milestone_units OBJECT EXCLUDE_FROM_ALL
        "${COS_NATIVE_ROOT}/src/pc/harness/pc_milestone.cpp"
        "${COS_NATIVE_ROOT}/vita/pc_milestone_support.cpp")
target_link_libraries(cos_vita_milestone_units PRIVATE cos_game_headers)
target_include_directories(cos_vita_milestone_units PRIVATE "${COS_NATIVE_ROOT}/src/pc")
target_compile_options(cos_vita_milestone_units PRIVATE -ffunction-sections -fdata-sections -g0)
set_property(TARGET cos_vita_milestone_units PROPERTY FOLDER "cos_native")

if (CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT TARGET aurora::gx)
    add_library(cos_vita_pc_stubs STATIC
            "${COS_NATIVE_ROOT}/vita/stubs_pc_features.cpp"
            "${COS_NATIVE_ROOT}/vita/stubs_dvd.cpp")
    target_include_directories(cos_vita_pc_stubs PUBLIC
            "${COS_NATIVE_ROOT}/include" ${COS_SDK_INCLUDE_DIRS})
    target_compile_definitions(cos_vita_pc_stubs PUBLIC TARGET_PC=1)
    set_property(TARGET cos_vita_pc_stubs PROPERTY FOLDER "cos_native")
    message(STATUS "cos_native: cos_vita_pc_stubs (PC features off, no disc path: the Vita source build)")

    set(COS_VITA_STATIC_INIT_UNITS
            "${COS_NATIVE_ROOT}/src/pc/harness/pc_static_init.cpp"
            "${COS_NATIVE_ROOT}/vita/vita_static_init.cpp"
            "${COS_ROOT}/src/f_pc/f_pc_profile.cpp"
            "${COS_ROOT}/src/f_pc/f_pc_profile_lst.cpp")
    add_library(cos_vita_static_init_probe OBJECT ${COS_VITA_STATIC_INIT_UNITS})
    target_link_libraries(cos_vita_static_init_probe PRIVATE cos_game_headers)
    set_property(TARGET cos_vita_static_init_probe PROPERTY FOLDER "cos_native")

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    # cos_sdk is part of the real link and defines the seventeen GX names the game units call
    # (GXExtras.cpp), so it is merged whole: the check then asks only "does this build's own code
    # still need the PC harness, Aurora DVD or a PC feature module?" and catches a stub that
    # collides with the SDK.
    add_custom_target(cos_vita_static_init_check
            COMMAND "${Python3_EXECUTABLE}" "${COS_NATIVE_ROOT}/vita/check_static_init_link.py"
                --nm "${CMAKE_NM}" --ld "${CMAKE_LINKER}"
                --output "${CMAKE_BINARY_DIR}/cos_vita_static_init_check.o"
                --stubs "$<TARGET_FILE:cos_vita_pc_stubs>"
                --stubs "$<TARGET_FILE:cos_vita_gx_stub>"
                --sdk "$<TARGET_FILE:cos_sdk>"
                $<TARGET_OBJECTS:cos_vita_static_init_probe>
                $<TARGET_OBJECTS:cos_vita_gx_stub_probe>
            DEPENDS cos_vita_pc_stubs cos_vita_static_init_probe cos_vita_gx_stub cos_vita_gx_stub_probe cos_sdk
            COMMAND_EXPAND_LISTS
            USES_TERMINAL
            COMMENT "Checking that the Vita units and static-init main need no PC harness, no Aurora DVD")
elseif (NOT CMAKE_CROSSCOMPILING)
    # Host test of the extracted check, against a fake profile list (check/pc_static_init_test.cpp).
    #   ninja cos_static_init_test && build/native-mac/cos_static_init_test
    add_executable(cos_static_init_test EXCLUDE_FROM_ALL
            "${COS_NATIVE_ROOT}/check/pc_static_init_test.cpp"
            "${COS_NATIVE_ROOT}/src/pc/harness/pc_static_init.cpp")
    target_link_libraries(cos_static_init_test PRIVATE cos_game_headers)
    set_target_properties(cos_static_init_test PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

    # The same boot unit on the host (the Vita executable cos_vita_boot is in cmake/vita.cmake):
    # Aurora's OSInit and VI and pc::staticInitCheck, with the bounded actor profile slice, no
    # pc_harness and no disc. Runtime evidence for native/vita/vita_boot.cpp; a Vita run is E2's.
    #   ninja cos_vita_boot_host && build/native-mac/cos_vita_boot_host
    # GNU binutils only, as the slice reduction needs readelf/objcopy/ld --gc-sections.
    if (COS_VITA_PROFILE_SLICE_OBJECT AND COS_VITA_FPC_CHAIN_OBJECT)
        add_executable(cos_vita_boot_host EXCLUDE_FROM_ALL
                "${COS_NATIVE_ROOT}/vita/vita_boot.cpp"
                "${COS_NATIVE_ROOT}/vita/vita_profile_list.cpp"
                "${COS_NATIVE_ROOT}/src/pc/harness/pc_static_init.cpp"
                "${COS_ROOT}/src/f_pc/f_pc_profile.cpp"
                "${COS_VITA_PROFILE_SLICE_OBJECT}"
                "${COS_VITA_FPC_CHAIN_OBJECT}"
                "${COS_VITA_DELETEHEAP_OBJECT}"
                "${COS_VITA_CULLING_OBJECT}"
                "${COS_VITA_DEMO_OBJECT}"
                "${COS_VITA_ASSERT_OBJECT}"
                "${COS_VITA_MAP_OBJECT}"
                $<TARGET_OBJECTS:cos_vita_milestone_units>)
        set_source_files_properties("${COS_VITA_PROFILE_SLICE_OBJECT}"
                "${COS_VITA_FPC_CHAIN_OBJECT}" "${COS_VITA_DELETEHEAP_OBJECT}"
                "${COS_VITA_CULLING_OBJECT}" "${COS_VITA_DEMO_OBJECT}"
                "${COS_VITA_ASSERT_OBJECT}" "${COS_VITA_MAP_OBJECT}" PROPERTIES
                EXTERNAL_OBJECT TRUE GENERATED TRUE)
        # Aurora's libraries reference each other in a cycle (core <-> gx/vi), as cos_sdk_smoke notes.
        target_link_libraries(cos_vita_boot_host PRIVATE cos_game_headers cos_sdk
                "-Wl,--start-group" ${COS_AURORA_LIBS} "-Wl,--end-group")
        target_link_options(cos_vita_boot_host PRIVATE -Wl,--gc-sections)
        add_dependencies(cos_vita_boot_host cos_vita_profile_slice cos_vita_fpc_chain
                cos_vita_deleteheap cos_vita_culling cos_vita_demo cos_vita_assert
                cos_vita_map cos_vita_milestone_units)
        set_target_properties(cos_vita_boot_host PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    endif ()

    # The ILP32 runtime test on the host: LP64 sizes/layout, atomics, heap, memcpy and the Aurora
    # OS/VI boot smoke. The ARM/ILP32 run needs Vita3K or hardware.
    #   ninja cos_ilp32_runtime_test && build/native-mac/cos_ilp32_runtime_test
    add_executable(cos_ilp32_runtime_test EXCLUDE_FROM_ALL
            "${COS_NATIVE_ROOT}/check/ilp32_runtime.cpp")
    target_link_libraries(cos_ilp32_runtime_test PRIVATE cos_game_headers cos_sdk
            "-Wl,--start-group" ${COS_AURORA_LIBS} "-Wl,--end-group")
    set_target_properties(cos_ilp32_runtime_test PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

    # The callable g_fpcLf_Method chain on its own: the five pointers are real and the
    # fpcLf_* -> fpcMtd_* path runs on a zeroed leaf (check/fpc_chain_test.cpp).
    #   ninja cos_vita_fpc_chain_test && build/native-mac/cos_vita_fpc_chain_test
    if (COS_VITA_FPC_CHAIN_OBJECT)
        add_executable(cos_vita_fpc_chain_test EXCLUDE_FROM_ALL
                "${COS_NATIVE_ROOT}/check/fpc_chain_test.cpp"
                "${COS_VITA_FPC_CHAIN_OBJECT}"
                "${COS_VITA_DELETEHEAP_OBJECT}"
                "${COS_VITA_CULLING_OBJECT}"
                "${COS_VITA_DEMO_OBJECT}"
                "${COS_VITA_ASSERT_OBJECT}"
                "${COS_VITA_MAP_OBJECT}")
        set_source_files_properties("${COS_VITA_FPC_CHAIN_OBJECT}"
                "${COS_VITA_DELETEHEAP_OBJECT}" "${COS_VITA_CULLING_OBJECT}"
                "${COS_VITA_DEMO_OBJECT}" "${COS_VITA_ASSERT_OBJECT}"
                "${COS_VITA_MAP_OBJECT}" PROPERTIES
                EXTERNAL_OBJECT TRUE GENERATED TRUE)
        target_link_libraries(cos_vita_fpc_chain_test PRIVATE cos_game_headers cos_sdk
                "-Wl,--start-group" ${COS_AURORA_LIBS} "-Wl,--end-group")
        target_link_options(cos_vita_fpc_chain_test PRIVATE -Wl,--gc-sections)
        add_dependencies(cos_vita_fpc_chain_test cos_vita_fpc_chain cos_vita_deleteheap
                cos_vita_culling cos_vita_demo cos_vita_assert cos_vita_map)
        set_target_properties(cos_vita_fpc_chain_test PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    endif ()

    # R1: the real dMap_2DT2_c::draw path with a bounded host GX/matrix backend.
    #   ninja cos_vita_map_j2d_test && build/native-mac/cos_vita_map_j2d_test
    if (COS_VITA_MAP_J2D_OBJECT)
        add_executable(cos_vita_map_j2d_test EXCLUDE_FROM_ALL
                "${COS_NATIVE_ROOT}/check/map_j2d_test.cpp"
                "${COS_NATIVE_ROOT}/check/map_j2d_stubs.cpp"
                "${COS_VITA_MAP_J2D_OBJECT}"
                $<TARGET_OBJECTS:cos_vita_j2d_units>)
        set_source_files_properties("${COS_VITA_MAP_J2D_OBJECT}" PROPERTIES
                EXTERNAL_OBJECT TRUE GENERATED TRUE)
        target_include_directories(cos_vita_map_j2d_test PRIVATE "${COS_NATIVE_ROOT}")
        target_link_libraries(cos_vita_map_j2d_test PRIVATE cos_game_headers)
        target_link_options(cos_vita_map_j2d_test PRIVATE -Wl,--gc-sections)
        add_dependencies(cos_vita_map_j2d_test cos_vita_map_j2d cos_vita_j2d_units)
        set_target_properties(cos_vita_map_j2d_test PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    endif ()

    # R2: real mapDrawAll enqueue + dDlst_list_c::draw2DOpa + the R1 J2D leaf.
    #   ninja cos_vita_map_draw_test && build/native-mac/cos_vita_map_draw_test
    if (COS_VITA_MAP_DRAW_OBJECT AND COS_VITA_DRAWLIST_OBJECT)
        add_executable(cos_vita_map_draw_test EXCLUDE_FROM_ALL
                "${COS_NATIVE_ROOT}/check/map_draw_test.cpp"
                "${COS_NATIVE_ROOT}/check/map_j2d_stubs.cpp"
                "${COS_VITA_MAP_DRAW_OBJECT}"
                "${COS_VITA_DRAWLIST_OBJECT}"
                $<TARGET_OBJECTS:cos_vita_j2d_units>)
        set_source_files_properties("${COS_VITA_MAP_DRAW_OBJECT}"
                "${COS_VITA_DRAWLIST_OBJECT}" PROPERTIES
                EXTERNAL_OBJECT TRUE GENERATED TRUE)
        target_include_directories(cos_vita_map_draw_test PRIVATE "${COS_NATIVE_ROOT}")
        target_link_libraries(cos_vita_map_draw_test PRIVATE cos_game_headers)
        target_link_options(cos_vita_map_draw_test PRIVATE -Wl,--gc-sections)
        add_dependencies(cos_vita_map_draw_test cos_vita_map_draw cos_vita_drawlist
                cos_vita_j2d_units)
        set_target_properties(cos_vita_map_draw_test PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    endif ()
endif ()
