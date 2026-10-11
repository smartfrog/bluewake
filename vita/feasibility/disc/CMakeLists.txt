cmake_minimum_required(VERSION 3.16)
project(ww13_disc C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(GXRUNTIME_DIR "${RECOMPCORE_DIR}/GXRuntime" CACHE PATH "Pinned GXRuntime (read-only)")
set(GXRUNTIME_ENABLE_AURORA OFF CACHE BOOL "" FORCE)
set(GXRUNTIME_ENABLE_AURORA_RECOMP OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

if(NOT EXISTS "${GXRUNTIME_DIR}/include/gxruntime/dvd.h")
    message(FATAL_ERROR "Missing pinned runtime at ${GXRUNTIME_DIR}; set RECOMPCORE_DIR")
endif()

add_subdirectory("${GXRUNTIME_DIR}" runtime)

add_executable(disc_probe disc_probe.c)
target_link_libraries(disc_probe PRIVATE gxruntime)
target_compile_options(disc_probe PRIVATE -O2 -Wall -Wextra
    -Werror=implicit-function-declaration -Werror=pointer-to-int-cast -Werror=int-to-pointer-cast)
