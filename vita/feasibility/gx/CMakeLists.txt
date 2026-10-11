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

add_executable(gx_replay gx_replay.cpp)
target_link_libraries(gx_replay PRIVATE GXRuntime::retail_gx_frontend GXRuntime::gxcore)
target_compile_options(gx_replay PRIVATE -O2 -Wall -Wextra)
