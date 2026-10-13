cmake_minimum_required(VERSION 3.16)
project(ww_capture_io_link C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(BLUEWAKE_ROOT "../../../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory(../diagnostic diagnostic)
add_executable(capture_io_link main.cpp
    "${BLUEWAKE_ROOT}/vita/host/frame_capture.cpp"
    "${BLUEWAKE_ROOT}/vita/host/mem1_capture.c"
    "${BLUEWAKE_ROOT}/vita/feasibility/armv7/platform.c"
    "${BLUEWAKE_ROOT}/vita/renderer/gx_frame_capture.cpp"
    "${BLUEWAKE_ROOT}/vita/feasibility/gx/png_capture.c")
set_source_files_properties("${BLUEWAKE_ROOT}/vita/feasibility/gx/png_capture.c" PROPERTIES LANGUAGE CXX)
set_source_files_properties("${BLUEWAKE_ROOT}/vita/feasibility/armv7/platform.c"
    PROPERTIES COMPILE_DEFINITIONS main=fixture_platform_main)
target_include_directories(capture_io_link PRIVATE
    "${BLUEWAKE_ROOT}/vita/host" "${BLUEWAKE_ROOT}/vita/renderer"
    "${BLUEWAKE_ROOT}/vita/feasibility/armv7" "${BLUEWAKE_ROOT}/runtime/host/src"
    "${BLUEWAKE_ROOT}/cmake/composite" "${BLUEWAKE_ROOT}/apple/ios/src"
    "${RECOMPCORE_DIR}/GXRuntime/include"
    "${RECOMPCORE_DIR}/Source/Core/Core/PowerPC/StaticRecomp")
target_compile_options(capture_io_link PRIVATE -O2 -Wall -Wextra -Werror)
target_link_libraries(capture_io_link PRIVATE ww_diagnostic_io)
if(VITA)
    target_compile_options(capture_io_link PRIVATE -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard)
    target_link_libraries(capture_io_link PRIVATE SceLibKernel_stub SceIofilemgr_stub SceProcessmgr_stub
        SceKernelThreadMgr_stub SceSysmem_stub)
endif()
