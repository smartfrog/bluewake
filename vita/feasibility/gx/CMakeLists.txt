cmake_minimum_required(VERSION 3.16)
project(ww02_gx C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(RENDERER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../renderer" ABSOLUTE)

if(VITA)
    # Vita side: the pinned frontend/gxcore for ARM plus the demo fixture,
    # submitted through vitaGL (Cg runtime shaders).
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
    set(GXRUNTIME_DIR "${RECOMPCORE_DIR}/GXRuntime" CACHE PATH "Pinned GXRuntime (read-only)")
    set(GXRUNTIME_ENABLE_AURORA OFF CACHE BOOL "" FORCE)
    set(GXRUNTIME_ENABLE_AURORA_RECOMP ON CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    if(NOT EXISTS "${GXRUNTIME_DIR}/graphics/frontend/include/gxruntime/aurora_recomp/retail_gx_frontend.hpp")
        message(FATAL_ERROR "Missing pinned frontend at ${GXRUNTIME_DIR}; set RECOMPCORE_DIR")
    endif()

    add_subdirectory("${GXRUNTIME_DIR}" runtime)

    add_library(gx_vita STATIC "${RENDERER_DIR}/gx_vita.cpp" "${RENDERER_DIR}/gx_cg.cpp")
    target_link_libraries(gx_vita PUBLIC GXRuntime::gxcore)
    target_include_directories(gx_vita PUBLIC "${RENDERER_DIR}")

    # The Vita3K-friendly (no-splash) vitaGL archive can be supplied here; the
    # default is the SDK's library.
    set(WW02_VITAGL_LIBRARY "vitaGL" CACHE STRING "vitaGL library name or absolute archive path")

    add_executable(gx_vita_demo gx_vita_main.cpp png_capture.c)
    target_link_libraries(gx_vita_demo PRIVATE
        gx_vita GXRuntime::retail_gx_frontend
        ${WW02_VITAGL_LIBRARY} vitashark SceShaccCgExt
        SceShaccCg_stub_weak SceGxm_stub_weak SceDisplay_stub_weak
        SceLibKernel_stub SceIofilemgr_stub SceProcessmgr_stub
        SceSysmodule_stub_weak SceCommonDialog_stub_weak SceAppMgr_stub_weak
        SceCtrl_stub_weak SceKernelDmacMgr_stub_weak taihen_stub_weak
        mathneon m stdc++)
    foreach(target gx_vita gx_vita_demo)
        target_compile_options(${target} PRIVATE -mcpu=cortex-a9 -mfpu=neon
            -mfloat-abi=hard -O2 -Wall -Wextra)
    endforeach()
    # vita-elf-create appends the SCE data (~3 KiB) at the end of segment 0 and
    # fails when segment 1 starts too close: anchor the RW base with margin.
    target_link_options(gx_vita_demo PRIVATE "-Wl,--section-start=.init_array=0x81100000")

    vita_create_self(eboot.bin gx_vita_demo)
    vita_create_vpk(ww02_gx_demo.vpk BWWG00002 eboot.bin VERSION 00.01
        NAME "WW02 GX vitaGL demo")
else()
    # Host side: the pinned-runtime probe and translation tests.
    set(GXRUNTIME_DIR "${RECOMPCORE_DIR}/GXRuntime" CACHE PATH "Pinned GXRuntime (read-only)")
    set(GXRUNTIME_ENABLE_AURORA OFF CACHE BOOL "" FORCE)
    set(GXRUNTIME_ENABLE_AURORA_RECOMP ON CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

    if(NOT EXISTS "${GXRUNTIME_DIR}/graphics/frontend/include/gxruntime/aurora_recomp/retail_gx_frontend.hpp")
        message(FATAL_ERROR "Missing pinned frontend at ${GXRUNTIME_DIR}; set RECOMPCORE_DIR")
    endif()

    add_subdirectory("${GXRUNTIME_DIR}" runtime)

    add_library(gx_vita STATIC "${RENDERER_DIR}/gx_vita.cpp" "${RENDERER_DIR}/gx_cg.cpp")
    target_link_libraries(gx_vita PUBLIC GXRuntime::gxcore)
    target_include_directories(gx_vita PUBLIC "${RENDERER_DIR}")
    target_compile_options(gx_vita PRIVATE -O2 -Wall -Wextra)

    add_executable(gx_replay gx_replay.cpp)
    target_link_libraries(gx_replay PRIVATE GXRuntime::retail_gx_frontend GXRuntime::gxcore)
    target_compile_options(gx_replay PRIVATE -O2 -Wall -Wextra)

    add_executable(gx_vita_tests "${RENDERER_DIR}/gx_vita_tests.cpp")
    target_link_libraries(gx_vita_tests PRIVATE gx_vita GXRuntime::retail_gx_frontend)
    target_compile_options(gx_vita_tests PRIVATE -O2 -Wall -Wextra)

    add_executable(gx_cg_tests "${RENDERER_DIR}/gx_cg_tests.cpp")
    target_link_libraries(gx_cg_tests PRIVATE gx_vita GXRuntime::retail_gx_frontend)
    target_compile_options(gx_cg_tests PRIVATE -O2 -Wall -Wextra)

    # VITA-WW-15: captured FIFO bytes -> pinned frontend -> plans ->
    # gx_vita translation -> Cg generation, reported with every refusal.
    add_executable(gx_bridge "${RENDERER_DIR}/gx_bridge.cpp" gx_bridge_main.cpp)
    target_link_libraries(gx_bridge PRIVATE gx_vita GXRuntime::retail_gx_frontend)
    target_compile_options(gx_bridge PRIVATE -O2 -Wall -Wextra)

    add_executable(gx_bridge_tests "${RENDERER_DIR}/gx_bridge_tests.cpp"
        "${RENDERER_DIR}/gx_bridge.cpp")
    target_link_libraries(gx_bridge_tests PRIVATE gx_vita GXRuntime::retail_gx_frontend)
    target_compile_options(gx_bridge_tests PRIVATE -O2 -Wall -Wextra)

    add_executable(png_capture_tests png_capture_tests.c png_capture.c)
    target_compile_options(png_capture_tests PRIVATE -O2 -Wall -Wextra)
endif()
