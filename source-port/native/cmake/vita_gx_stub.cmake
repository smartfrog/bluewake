# E3 POC (build-f5/source-port-next-pa/REPORT.md, blocker 1): the Vita source link builds without
# a renderer (AURORA_ENABLE_GX=OFF), so Aurora's aurora_gx and aurora_gd do not exist and the
# game's GX/GD references have no definition. This file adds the stub that defines them
# (native/vita/stubs_gx.cpp, stubs_gd.cpp) and the focused check that proves it covers the
# references of representative game units.
#
# What is proven here (32-bit Vita builds only):
# - cos_vita_gx_stub_probe compiles seven game units that draw through GX (JSystem 2D, JParticle,
#   JFWDisplay, JUTResFont) for ARMv7/ILP32; the check merges cos_sdk whole, so it also covers the
#   GF objects cos_sdk already builds.
# - cos_vita_gx_stub_check resolves every GX/GD symbol those objects reference against
#   libcos_vita_gx_stub.a with ld -r, so the link has nothing left to guess.
#
# Not here yet: wiring the stub into a game executable. The reduced Vita harness (pc_smoke split,
# blocker 2 of the P-A report) does not exist, so there is no Vita executable to link it into.
#
# Host builds keep aurora_gx and never build these targets (the stub would duplicate its symbols).
include_guard(GLOBAL)

set(COS_VITA_GX_STUB_SOURCES
        "${COS_NATIVE_ROOT}/vita/stubs_gx.cpp"
        "${COS_NATIVE_ROOT}/vita/stubs_gd.cpp")

if (CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT TARGET aurora::gx)
    add_library(cos_vita_gx_stub STATIC ${COS_VITA_GX_STUB_SOURCES})
    target_include_directories(cos_vita_gx_stub PUBLIC ${COS_SDK_INCLUDE_DIRS})
    target_compile_definitions(cos_vita_gx_stub PUBLIC TARGET_PC=1)
    set_property(TARGET cos_vita_gx_stub PROPERTY FOLDER "cos_native")
    message(STATUS "cos_native: cos_vita_gx_stub (the Vita source link has no aurora_gx/aurora_gd)")

    # Representative GXBegin callers, picked for the GX surface they use and because they include
    # no assets/ headers (the stub asset headers are a separate P-A item):
    # - J2DGrafContext/J2DScreen/J2DPicture/J2DWindow: J2D's viewport, scissor, TEV and
    #   texture-object path;
    # - JPADrawVisitor: particles, vertex descriptors and the GS/PE state;
    # - JFWDisplay: the frame copy path (GXCopyDisp, GXGetGPStatus, GXGetYScaleFactor);
    # - JUTResFont: GXBegin for glyph quads plus texture initialization.
    # m_Do_ext.cpp, d_drawlist.cpp, d_cloth_packet.cpp and d_menu_cloth.cpp are the four GXBegin
    # callers that include assets/ headers generated from the disc, so they wait for the --stubs
    # item; the other twelve (d_map.cpp, d_gameover.cpp, ...) include res/ headers and join once
    # the res-only headers are generated for this build.
    set(COS_VITA_GX_STUB_PROBE_UNITS
            "${COS_ROOT}/src/JSystem/J2DGraph/J2DGrafContext.cpp"
            "${COS_ROOT}/src/JSystem/J2DGraph/J2DScreen.cpp"
            "${COS_ROOT}/src/JSystem/J2DGraph/J2DPicture.cpp"
            "${COS_ROOT}/src/JSystem/J2DGraph/J2DWindow.cpp"
            "${COS_ROOT}/src/JSystem/JParticle/JPADrawVisitor.cpp"
            "${COS_ROOT}/src/JSystem/JFramework/JFWDisplay.cpp"
            "${COS_ROOT}/src/JSystem/JUtility/JUTResFont.cpp")
    add_library(cos_vita_gx_stub_probe OBJECT ${COS_VITA_GX_STUB_PROBE_UNITS})
    target_link_libraries(cos_vita_gx_stub_probe PRIVATE cos_game_headers)

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    # cos_sdk carries the GF objects (cmake/sdk_gf.cmake adds them to it), so the whole SDK archive
    # is merged into the check: it is part of the real link and defines seventeen GX names itself.
    set(_cos_vita_gx_check_objects
            "$<TARGET_OBJECTS:cos_vita_gx_stub_probe>")
    add_custom_target(cos_vita_gx_stub_check
            COMMAND "${Python3_EXECUTABLE}" "${COS_NATIVE_ROOT}/vita/check_gx_symbols.py"
                --nm "${CMAKE_NM}" --ld "${CMAKE_LINKER}"
                --stub "$<TARGET_FILE:cos_vita_gx_stub>"
                --sdk "$<TARGET_FILE:cos_sdk>"
                --output "${CMAKE_BINARY_DIR}/cos_vita_gx_stub_check.o"
                ${_cos_vita_gx_check_objects}
            DEPENDS cos_vita_gx_stub cos_vita_gx_stub_probe cos_sdk
            COMMAND_EXPAND_LISTS
            USES_TERMINAL
            COMMENT "Checking that the Vita GX/GD stub resolves the representative game objects")
elseif (NOT CMAKE_CROSSCOMPILING AND TARGET aurora::gx)
    # Host test of the stub's counters and state (native/vita/gx_stub_test.cpp). Not part of
    # `all`, as cos_sdk_smoke_tsan: a host build has the real aurora_gx, so the stub is only
    # built for this test.
    #   ninja cos_vita_gx_stub_test && build/native-mac/cos_vita_gx_stub_test
    add_executable(cos_vita_gx_stub_test EXCLUDE_FROM_ALL
            ${COS_VITA_GX_STUB_SOURCES}
            "${COS_NATIVE_ROOT}/vita/gx_stub_test.cpp")
    target_include_directories(cos_vita_gx_stub_test PRIVATE
            "${COS_NATIVE_ROOT}/vita" ${COS_SDK_INCLUDE_DIRS})
    target_compile_definitions(cos_vita_gx_stub_test PRIVATE TARGET_PC=1)
    set_target_properties(cos_vita_gx_stub_test PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif ()
