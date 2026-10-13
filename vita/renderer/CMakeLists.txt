cmake_minimum_required(VERSION 3.16)
project(ww18_image_replay C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../feasibility/gx" gx)
set(PNG_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../feasibility/gx/png_capture.c")

if(VITA)
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
    add_executable(gx_image_replay gx_image_main.cpp gx_frame_capture.cpp gx_bridge.cpp "${PNG_SOURCE}")
    target_link_libraries(gx_image_replay PRIVATE "-Wl,--start-group" ww_renderer gx_vita
        GXRuntime::retail_gx_frontend GXRuntime::gxcore ${WW02_VITAGL_LIBRARY}
        vitashark SceShaccCgExt SceShaccCg_stub_weak SceGxm_stub_weak
        SceDisplay_stub_weak SceLibKernel_stub SceIofilemgr_stub SceProcessmgr_stub
        SceSysmodule_stub_weak SceCommonDialog_stub_weak SceAppMgr_stub_weak
        SceCtrl_stub_weak SceKernelDmacMgr_stub_weak taihen_stub_weak mathneon m stdc++
        "-Wl,--end-group")
    target_compile_options(gx_image_replay PRIVATE -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -O2 -Wall -Wextra)
    target_link_options(gx_image_replay PRIVATE "-Wl,--section-start=.init_array=0x81200000")
    vita_create_self(image-eboot.bin gx_image_replay)
    vita_create_vpk(gx_image_replay.vpk BWWG00002 image-eboot.bin VERSION 00.01 NAME "WW18 private GX image replay")
else()
    add_executable(gx_frame_capture_tests gx_frame_capture_tests.cpp gx_frame_capture.cpp "${PNG_SOURCE}")
    target_link_libraries(gx_frame_capture_tests PRIVATE gx_vita)
    target_compile_options(gx_frame_capture_tests PRIVATE -Wall -Wextra)
endif()
