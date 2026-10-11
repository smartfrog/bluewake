cmake_minimum_required(VERSION 3.16)
project(ww02_gx C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(GXRUNTIME_DIR "${RECOMPCORE_DIR}/GXRuntime" CACHE PATH "Pinned GXRuntime (read-only)")
set(GXRUNTIME_ENABLE_AURORA OFF CACHE BOOL "" FORCE)
set(GXRUNTIME_ENABLE_AURORA_RECOMP ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

if(NOT EXISTS "${GXRUNTIME_DIR}/graphics/frontend/include/gxruntime/aurora_recomp/retail_gx_frontend.hpp")
    message(FATAL_ERROR "Missing pinned frontend at ${GXRUNTIME_DIR}; set RECOMPCORE_DIR")
endif()

add_subdirectory("${GXRUNTIME_DIR}" runtime)

set(RENDERER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../renderer")

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
