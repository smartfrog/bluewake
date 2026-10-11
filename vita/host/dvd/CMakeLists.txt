cmake_minimum_required(VERSION 3.16)
project(ww13_dvd C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(RECOMPCORE_DIR "" CACHE PATH "Pinned RecompCore checkout (read-only)")
option(WW13_TESTS_ONLY "Build game-free host tests only" ON)

add_library(ww13_dvd_io STATIC dvd_io.c)
target_include_directories(ww13_dvd_io PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ww13_dvd_io PRIVATE -Wall -Wextra -Werror)

if(WW13_TESTS_ONLY)
    enable_testing()
    add_executable(dvd_io_test dvd_io_test.c dvd_fake_backend.c)
    target_link_libraries(dvd_io_test PRIVATE ww13_dvd_io)
    target_compile_options(dvd_io_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME dvd_io_cases COMMAND dvd_io_test all)

    # The pinned-runtime probe is built only when the read-only pin is given;
    # the seam contract tests above never need it.
    if(RECOMPCORE_DIR)
    set(GXRUNTIME_DIR "${RECOMPCORE_DIR}/GXRuntime")
    if(NOT EXISTS "${GXRUNTIME_DIR}/include/gxruntime/dvd.h")
        message(FATAL_ERROR "Missing pinned runtime at ${GXRUNTIME_DIR}; set RECOMPCORE_DIR")
    endif()
    set(GXRUNTIME_ENABLE_AURORA OFF CACHE BOOL "" FORCE)
    set(GXRUNTIME_ENABLE_AURORA_RECOMP OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory("${GXRUNTIME_DIR}" runtime)

    # The pinned dvd.c compiled byte-identical: only its four stdio primitives
    # are redirected to the seam. Linked before gxruntime so the archive's
    # dvd.c.o is never extracted.
    add_library(ww13_dvd_pin OBJECT "${GXRUNTIME_DIR}/src/dvd.c")
    target_include_directories(ww13_dvd_pin PRIVATE "${GXRUNTIME_DIR}/include")
    target_compile_options(ww13_dvd_pin PRIVATE
        "-include" "${CMAKE_CURRENT_SOURCE_DIR}/dvd_seam.h"
        -O2 -ffp-contract=off -fno-fast-math -Wall -Wextra)

    add_executable(dvd_probe dvd_probe.c dvd_fake_backend.c $<TARGET_OBJECTS:ww13_dvd_pin>)
    target_link_libraries(dvd_probe PRIVATE ww13_dvd_io gxruntime)
    target_compile_options(dvd_probe PRIVATE -O2 -Wall -Wextra -Werror)
    endif()
endif()
