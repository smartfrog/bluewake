cmake_minimum_required(VERSION 3.16)
project(ww05_startup C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(WW05_TESTS_ONLY "Build game-free host contract tests only" OFF)
option(WW05_ENABLE_VI_CLOCK "Experimental guest-cycle VI clock for private boot trials" OFF)
option(WW05_ENABLE_PAD_INPUT "Experimental Vita controls sampled only by guest SI requests" OFF)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ww05_pad_input.json"
    CONTENT "{\"option\":\"WW05_ENABLE_PAD_INPUT\",\"enabled\":$<IF:$<BOOL:${WW05_ENABLE_PAD_INPUT}>,true,false>,\"source\":\"$<IF:$<BOOL:${WW05_ENABLE_PAD_INPUT}>,sceCtrlPeekBufferPositive,empty-legacy>\",\"sampling\":\"guest-SI-only\"}\n")
get_filename_component(BLUEWAKE_ROOT "../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(GXRUNTIME_DIR "${RECOMPCORE_DIR}/GXRuntime" CACHE PATH "Pinned runtime" FORCE)
set(ABI_DIR "${RECOMPCORE_DIR}/Source/Core/Core/PowerPC/StaticRecomp" CACHE PATH "Pinned module ABI" FORCE)
set(GXRUNTIME_ENABLE_AURORA OFF CACHE BOOL "" FORCE)
# VITA-WW-17: the renderer consumes the pinned RetailGxFrontend and gxcore.
set(GXRUNTIME_ENABLE_AURORA_RECOMP ON CACHE BOOL "" FORCE)
set(BUILD_TESTING ${WW05_TESTS_ONLY} CACHE BOOL "" FORCE)
add_subdirectory("${GXRUNTIME_DIR}" runtime)

# VITA-WW-17: the host-facing renderer (vita/renderer). On the Vita it submits
# through vitaGL; the tests-only host build links the same core with the NULL
# platform backend.
set(WW17_RENDERER_DIR "${BLUEWAKE_ROOT}/vita/renderer" CACHE PATH "VITA-WW-17 renderer sources")
set(WW17_RENDERER_SOURCES
    "${WW17_RENDERER_DIR}/ww_renderer.cpp"
    "${WW17_RENDERER_DIR}/gx_bridge.cpp"
    "${WW17_RENDERER_DIR}/gx_vita.cpp"
    "${WW17_RENDERER_DIR}/gx_cg.cpp"
    "${WW17_RENDERER_DIR}/gx_frame_capture.cpp"
    "${BLUEWAKE_ROOT}/vita/feasibility/gx/png_capture.c")
set_source_files_properties("${BLUEWAKE_ROOT}/vita/feasibility/gx/png_capture.c" PROPERTIES LANGUAGE CXX)
if(VITA)
    list(APPEND WW17_RENDERER_SOURCES "${WW17_RENDERER_DIR}/vita_gl_backend.cpp")
endif()
add_library(ww_renderer STATIC ${WW17_RENDERER_SOURCES})
target_include_directories(ww_renderer PUBLIC "${WW17_RENDERER_DIR}")
target_link_libraries(ww_renderer PUBLIC GXRuntime::retail_gx_frontend GXRuntime::gxcore)
target_compile_options(ww_renderer PRIVATE -O2 -Wall -Wextra)
set(WW13_TESTS_ONLY OFF CACHE BOOL "" FORCE)
set(WW13_VITA OFF CACHE BOOL "" FORCE)
add_subdirectory(dvd)
target_compile_options(gxruntime PRIVATE -ffp-contract=off -fno-fast-math -fstack-usage)

add_library(ww05_services STATIC boot_host.c gx_checkpoint.c gx_stream.c fifo_gather.c fifo_consumer.c
    code_guard.c dsp_interface.c bat_contract.c disc_service.c rel_provider.c rel_service.c rel_link.c
    scheduler_observation.c vi_clock.c gx_draw_observation.c gpu_finish.c frame_timing.c mem1_capture.c frame_capture.cpp
    dsp_rom_mail.c dsp_deferred.c pad_input.c
    $<TARGET_OBJECTS:ww13_dvd_pin>
    "${BLUEWAKE_ROOT}/runtime/host/src/cycle_domain.c"
    "${BLUEWAKE_ROOT}/runtime/host/src/aram_dma.c"
    "${BLUEWAKE_ROOT}/runtime/host/src/external_memory.c"
    "${BLUEWAKE_ROOT}/runtime/host/src/ipl_sram.c"
    "${BLUEWAKE_ROOT}/runtime/host/src/pad_wire.c"
    "${BLUEWAKE_ROOT}/runtime/host/src/callback_delivery.c"
    "${BLUEWAKE_ROOT}/runtime/host/src/scheduler_contract.c"
    "${BLUEWAKE_ROOT}/runtime/host/src/rel_scratch_allocator.c"
    "${BLUEWAKE_ROOT}/runtime/host/src/interrupt_sources.c")
target_include_directories(ww05_services PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    "${BLUEWAKE_ROOT}/runtime/host/src" "${BLUEWAKE_ROOT}/cmake/composite" "${ABI_DIR}"
    "${BLUEWAKE_ROOT}/apple/ios/src")
target_include_directories(ww05_services PRIVATE "${BLUEWAKE_ROOT}/vita/feasibility/armv7")
target_link_libraries(ww05_services PUBLIC ww13_dvd_io GXRuntime::runtime ww_renderer)
target_compile_definitions(ww05_services PRIVATE WW05_ENABLE_VI_CLOCK=$<BOOL:${WW05_ENABLE_VI_CLOCK}>)
target_compile_definitions(ww05_services PUBLIC WW05_ENABLE_PAD_INPUT=$<BOOL:${WW05_ENABLE_PAD_INPUT}>)
if(VITA AND WW05_ENABLE_PAD_INPUT)
    target_sources(ww05_services PRIVATE pad_vita.c)
    target_link_libraries(ww05_services PUBLIC SceCtrl_stub)
endif()
target_compile_options(ww05_services PRIVATE -O2 -ffp-contract=off -fno-fast-math -fstack-usage
    -Wall -Wextra $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>)

if(WW05_TESTS_ONLY)
    enable_testing()
    add_executable(pad_input_probe pad_input_probe.c)
    target_compile_options(pad_input_probe PRIVATE -UNDEBUG)
    target_link_libraries(pad_input_probe PRIVATE ww05_services)
    add_executable(pad_wire_test "${BLUEWAKE_ROOT}/tests/pad_wire_test.c")
    target_link_libraries(pad_wire_test PRIVATE ww05_services)
    add_test(NAME pad-wire COMMAND pad_wire_test)
    foreach(scenario mapping live live-128 status reset origin recalibrate origin-padded recalibrate-padded
                     empty-1 empty-2 empty-3 legacy-buffer-zero legacy-buffer-eight
                     consume-high consume-low poll-0 poll-80 poll-40 poll-20 poll-10 poll-f0
                     release release-poll absent-transfer absent-poll error-transfer error-poll
                     unknown unknown-short invalid-count legacy-poll credit-transfer credit-poll reconnect)
        add_test(NAME pad-input-${scenario} COMMAND pad_input_probe ${scenario})
        set_tests_properties(pad-input-${scenario} PROPERTIES LABELS "synthetic;pad")
    endforeach()
    foreach(channel RANGE 0 3)
        add_test(NAME pad-input-legacy-transfer-${channel} COMMAND host_probe si-empty-${channel})
    endforeach()
    add_test(NAME pad-input-legacy-ack COMMAND host_probe si-empty-ack)
    set(synthetic_init_bytes "")
    foreach(value RANGE 0 127)
        string(APPEND synthetic_init_bytes "${value},")
    endforeach()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ww05_dsp_init.h"
        "static const unsigned char ww05_dsp_init_reference[128] = {${synthetic_init_bytes}};\n")
    target_include_directories(ww05_services PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_sources(ww05_services PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ww05_external_vector.c"
                                       "${CMAKE_CURRENT_BINARY_DIR}/ww05_fp_vector.c"
                                       "${CMAKE_CURRENT_BINARY_DIR}/ww05_decrementer_vector.c")
    add_executable(host_probe host_probe.c)
    target_compile_options(host_probe PRIVATE -UNDEBUG)
    target_link_libraries(host_probe PRIVATE ww05_services)
    target_link_options(host_probe PRIVATE -Wl,--wrap=calloc -Wl,--wrap=fprintf)
    add_executable(aram_probe aram_probe.c)
    target_compile_options(aram_probe PRIVATE -UNDEBUG)
    target_link_libraries(aram_probe PRIVATE ww05_services)
    add_executable(dsp_init_probe dsp_init_probe.c)
    target_compile_options(dsp_init_probe PRIVATE -UNDEBUG)
    target_link_libraries(dsp_init_probe PRIVATE ww05_services)
    target_link_options(dsp_init_probe PRIVATE -Wl,--wrap=fprintf)
    add_executable(bat_probe bat_probe.c)
    target_compile_options(bat_probe PRIVATE -UNDEBUG)
    target_link_libraries(bat_probe PRIVATE ww05_services)
    add_executable(external_vector_probe external_vector_probe.c)
    target_compile_options(external_vector_probe PRIVATE -UNDEBUG)
    target_link_libraries(external_vector_probe PRIVATE ww05_services)
    add_executable(di_inquiry_probe di_inquiry_probe.c)
    target_compile_options(di_inquiry_probe PRIVATE -UNDEBUG)
    target_link_libraries(di_inquiry_probe PRIVATE ww05_services)
    add_executable(di_read_probe di_read_probe.c)
    target_compile_options(di_read_probe PRIVATE -UNDEBUG)
    target_link_libraries(di_read_probe PRIVATE ww05_services)
    target_link_options(di_read_probe PRIVATE -Wl,--wrap=fprintf)
    add_executable(dvd_open_probe dvd_open_probe.c)
    target_compile_options(dvd_open_probe PRIVATE -UNDEBUG)
    target_link_libraries(dvd_open_probe PRIVATE ww05_services)
    target_link_options(dvd_open_probe PRIVATE -Wl,--wrap=fprintf)
    add_executable(dvd_read_probe dvd_read_probe.c)
    target_compile_options(dvd_read_probe PRIVATE -UNDEBUG)
    target_link_libraries(dvd_read_probe PRIVATE ww05_services)
    target_link_options(dvd_read_probe PRIVATE -Wl,--wrap=fprintf)
    add_executable(dvd_callback_probe dvd_callback_probe.c)
    target_compile_options(dvd_callback_probe PRIVATE -UNDEBUG)
    target_link_libraries(dvd_callback_probe PRIVATE ww05_services)
    target_link_options(dvd_callback_probe PRIVATE -Wl,--wrap=fprintf)
    add_executable(rel_materialize_probe rel_materialize_probe.c)
    target_compile_options(rel_materialize_probe PRIVATE -UNDEBUG)
    target_link_libraries(rel_materialize_probe PRIVATE ww05_services)
    target_link_options(rel_materialize_probe PRIVATE -Wl,--wrap=fprintf -Wl,--wrap=calloc -Wl,--wrap=free)
    add_executable(scheduler_observation_probe scheduler_observation_probe.c)
    target_compile_options(scheduler_observation_probe PRIVATE -UNDEBUG)
    target_link_libraries(scheduler_observation_probe PRIVATE ww05_services)
    target_link_options(scheduler_observation_probe PRIVATE -Wl,--wrap=fprintf)
    add_executable(vi_clock_probe vi_clock_probe.c)
    target_compile_options(vi_clock_probe PRIVATE -UNDEBUG)
    target_link_libraries(vi_clock_probe PRIVATE ww05_services)
    target_link_options(vi_clock_probe PRIVATE -Wl,--wrap=fprintf)
    add_executable(gather_probe gather_probe.c)
    target_compile_options(gather_probe PRIVATE -UNDEBUG)
    target_link_libraries(gather_probe PRIVATE ww05_services)
    target_link_options(gather_probe PRIVATE -Wl,--wrap=fprintf)
    add_executable(cp_clear_probe cp_clear_probe.c)
    target_compile_options(cp_clear_probe PRIVATE -UNDEBUG)
    target_link_libraries(cp_clear_probe PRIVATE ww05_services)
    add_executable(retained_register_probe retained_register_probe.c)
    target_compile_options(retained_register_probe PRIVATE -UNDEBUG)
    target_link_libraries(retained_register_probe PRIVATE ww05_services)
    add_executable(fifo_consumer_probe fifo_consumer_probe.c)
    target_compile_options(fifo_consumer_probe PRIVATE -UNDEBUG)
    target_link_libraries(fifo_consumer_probe PRIVATE ww05_services)
    target_link_options(fifo_consumer_probe PRIVATE -Wl,--wrap=fprintf)
    add_executable(gx_stream_probe gx_stream_probe.c)
    target_compile_options(gx_stream_probe PRIVATE -UNDEBUG)
    target_link_libraries(gx_stream_probe PRIVATE ww05_services)
    target_link_options(gx_stream_probe PRIVATE -Wl,--wrap=fwrite)
    add_executable(gx_draw_probe gx_draw_probe.c)
    target_compile_options(gx_draw_probe PRIVATE -UNDEBUG)
    target_link_libraries(gx_draw_probe PRIVATE ww05_services)
    target_link_options(gx_draw_probe PRIVATE -Wl,--wrap=fprintf)
    add_executable(gx_drain_probe gx_drain_probe.cpp)
    target_compile_options(gx_drain_probe PRIVATE -UNDEBUG)
    target_link_libraries(gx_drain_probe PRIVATE ww05_services)
    target_link_options(gx_drain_probe PRIVATE -Wl,--wrap=clock_gettime -Wl,--wrap=fprintf
        -Wl,--wrap=fwrite -Wl,--wrap=fflush -Wl,--wrap=fclose -Wl,--wrap=rename)
    add_executable(gpu_finish_probe gpu_finish_probe.cpp)
    target_compile_options(gpu_finish_probe PRIVATE -UNDEBUG)
    target_link_libraries(gpu_finish_probe PRIVATE ww05_services)
    target_link_options(gpu_finish_probe PRIVATE -Wl,--wrap=fprintf)
    add_executable(wpar_probe wpar_probe.c)
    target_compile_options(wpar_probe PRIVATE -UNDEBUG)
    target_link_libraries(wpar_probe PRIVATE ww05_services)
    target_link_options(wpar_probe PRIVATE -Wl,--wrap=fprintf)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ww05_identity.h" "#define WW05_SOURCE \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"\n")
    add_executable(io_probe main.c io_probe.c io_renderer_fixture.cpp
        "${BLUEWAKE_ROOT}/vita/feasibility/armv7/platform.c"
        "${BLUEWAKE_ROOT}/vita/feasibility/armv7/abi.c")
    target_include_directories(io_probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
        "${BLUEWAKE_ROOT}/vita/feasibility/armv7")
    target_compile_definitions(io_probe PRIVATE WW05_IO_TEST=1)
    target_compile_options(io_probe PRIVATE -fno-builtin)
    target_link_libraries(io_probe PRIVATE ww05_services)
    target_link_options(io_probe PRIVATE -Wl,--wrap=fopen -Wl,--wrap=fprintf -Wl,--wrap=fflush
        -Wl,--wrap=fclose -Wl,--wrap=rename -Wl,--wrap=fixture_log_close -Wl,--wrap=setvbuf)
else()
    if(NOT VITA)
        message(FATAL_ERROR "The real game target is Vita-only; host execution is not authorized")
    endif()
    foreach(input "${WW05_IDENTITY_DIR}/ww05_identity.h" "${WW05_IDENTITY_DIR}/ww05_dsp_init.h"
                  "${WW05_IDENTITY_DIR}/ww05_external_vector.c"
                  "${WW05_IDENTITY_DIR}/ww05_fp_vector.c"
                  "${WW05_IDENTITY_DIR}/ww05_decrementer_vector.c" "${WW05_DOL}")
        if(NOT EXISTS "${input}")
            message(FATAL_ERROR "Missing verified private input: ${input}")
        endif()
    endforeach()
    target_include_directories(ww05_services PRIVATE "${WW05_IDENTITY_DIR}")
    target_sources(ww05_services PRIVATE "${WW05_IDENTITY_DIR}/ww05_external_vector.c"
                                       "${WW05_IDENTITY_DIR}/ww05_fp_vector.c"
                                       "${WW05_IDENTITY_DIR}/ww05_decrementer_vector.c")
    set(BLUEWAKE_STATIC_MODULE ON CACHE BOOL "" FORCE)
    set(WW05_MODULE_OPT_LEVEL 1 CACHE STRING "Translated module optimization level (1, 2, or 3)")
    set(COMPOSITE_OPTIMIZATION_LEVEL ${WW05_MODULE_OPT_LEVEL} CACHE STRING "" FORCE)
    add_subdirectory("${BLUEWAKE_ROOT}/cmake/composite" composite)
    target_compile_options(gGZLE01_recomp PRIVATE -fno-tree-dominator-opts -fno-fast-math -fstack-usage)
    add_executable(ww05_host main.c "${BLUEWAKE_ROOT}/vita/feasibility/armv7/platform.c"
        "${BLUEWAKE_ROOT}/vita/feasibility/armv7/abi.c")
    target_include_directories(ww05_host PRIVATE "${WW05_IDENTITY_DIR}"
        "${BLUEWAKE_ROOT}/vita/feasibility/armv7")
    set(WW05_VITAGL_LIBRARY vitaGL CACHE STRING "No-splash vitaGL archive")
    set(WW05_RENDERER_SDK "" CACHE PATH "SDK target directory supplying vitaGL headers and support libraries")
    if(WW05_RENDERER_SDK)
        if(NOT EXISTS "${WW05_RENDERER_SDK}/include/vitaGL.h" OR
           NOT EXISTS "${WW05_RENDERER_SDK}/lib/libvitashark.a")
            message(FATAL_ERROR "Missing renderer SDK headers or support libraries")
        endif()
        target_include_directories(ww_renderer SYSTEM PRIVATE "${WW05_RENDERER_SDK}/include")
        target_link_directories(ww05_host PRIVATE "${WW05_RENDERER_SDK}/lib")
    endif()
    target_link_libraries(ww05_host PRIVATE gGZLE01_recomp ww05_services
        SceLibKernel_stub SceKernelThreadMgr_stub SceIofilemgr_stub SceProcessmgr_stub SceSysmem_stub)
    target_link_libraries(ww_renderer PUBLIC
        "${WW05_VITAGL_LIBRARY}" vitashark SceShaccCgExt SceShaccCg_stub_weak SceGxm_stub_weak
        SceDisplay_stub_weak SceSysmodule_stub_weak SceCommonDialog_stub_weak SceAppMgr_stub_weak
        SceCtrl_stub_weak SceKernelDmacMgr_stub_weak taihen_stub_weak mathneon m
        SceLibKernel_stub SceIofilemgr_stub SceProcessmgr_stub)
    target_compile_options(ww05_host PRIVATE -O2 -ffp-contract=off -fno-fast-math -fstack-usage)
    target_link_options(ww05_host PRIVATE "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/ww05.map")
    foreach(target gxruntime ww05_services ww13_dvd_io ww13_dvd_pin gGZLE01_recomp ww05_host ww_renderer)
        target_compile_options(${target} PRIVATE -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard)
    endforeach()
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
    vita_create_self(eboot.bin ww05_host)
    vita_create_vpk(ww05.vpk BWWG00005 eboot.bin VERSION 00.01 NAME "WW startup GX frontier"
        FILE "${WW05_DOL}" main.dol)
endif()
