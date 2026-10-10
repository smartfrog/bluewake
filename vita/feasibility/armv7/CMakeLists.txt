cmake_minimum_required(VERSION 3.16)
project(ww_armv7_fixture C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(BLUEWAKE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(WW_MODE "corpus" CACHE STRING "corpus or contract")
set(GXRUNTIME_DIR "${RECOMPCORE_DIR}/GXRuntime")
set(ABI_DIR "${RECOMPCORE_DIR}/Source/Core/Core/PowerPC/StaticRecomp")
foreach(required "${GXRUNTIME_DIR}/include/core/cpu.h" "${FIXTURE_DIR}/corpus.c"
        "${FIXTURE_DIR}/test_c_execute.c" "${FIXTURE_DIR}/composite/generated.h")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "Missing fixture input: ${required}; run scripts/vita/armv7/build.py")
    endif()
endforeach()

add_library(ww_cpu STATIC
    "${GXRUNTIME_DIR}/src/core/cpu.c"
    "${GXRUNTIME_DIR}/src/core/cpu_exception.c"
    "${GXRUNTIME_DIR}/src/core/cpu_interpreter.c"
    "${GXRUNTIME_DIR}/src/core/cpu_interpreter_table.c"
    "${GXRUNTIME_DIR}/src/core/cpu_interpreter_float.c"
    "${GXRUNTIME_DIR}/src/core/cpu_interpreter_integer.c"
    "${GXRUNTIME_DIR}/src/guest_memory.c"
    "${GXRUNTIME_DIR}/src/guest_memory_dirty.c")
target_include_directories(ww_cpu PUBLIC "${GXRUNTIME_DIR}/include")
target_compile_options(ww_cpu PRIVATE -O2 -ffp-contract=off -fno-fast-math -fstack-usage)

if(WW_MODE STREQUAL "corpus")
    add_executable(ww_fixture corpus_main.c abi.c platform.c
        "${FIXTURE_DIR}/corpus.c" "${FIXTURE_DIR}/test_c_execute.c")
    set_source_files_properties("${FIXTURE_DIR}/test_c_execute.c" PROPERTIES
        COMPILE_DEFINITIONS main=donor_main)
    set_source_files_properties("${FIXTURE_DIR}/corpus.c" PROPERTIES
        COMPILE_OPTIONS "-include;${FIXTURE_DIR}/corpus_inline.h")
elseif(WW_MODE STREQUAL "contract")
    file(GLOB chunks "${FIXTURE_DIR}/composite/chunks_dol/*.c")
    add_executable(ww_fixture contract_main.c contract_dispatch.c abi.c platform.c
        "${BLUEWAKE_ROOT}/cmake/composite/direct_calls.c" ${chunks})
    target_include_directories(ww_fixture PRIVATE "${FIXTURE_DIR}/composite"
        "${BLUEWAKE_ROOT}/cmake/composite" "${ABI_DIR}")
    target_compile_definitions(ww_fixture PRIVATE BLUEWAKE_EDGE_FILTER=1)
    set_source_files_properties(${chunks} PROPERTIES COMPILE_OPTIONS
        "-include;${BLUEWAKE_ROOT}/cmake/composite/hidden_externs.h")
else()
    message(FATAL_ERROR "WW_MODE must be corpus or contract")
endif()
target_compile_definitions(ww_fixture PRIVATE DOLRECOMP_CPU_HEADER="core/cpu.h")
target_compile_options(ww_fixture PRIVATE -O2 -ffp-contract=off -fno-fast-math -fstack-usage
    -Wall -Wextra -Werror=implicit-function-declaration -Werror=pointer-to-int-cast
    -Werror=int-to-pointer-cast)
target_link_libraries(ww_fixture PRIVATE ww_cpu m)
target_link_options(ww_fixture PRIVATE "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/fixture.map")

if(VITA)
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
    foreach(target ww_cpu ww_fixture)
        target_compile_options(${target} PRIVATE -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard)
    endforeach()
    target_link_libraries(ww_fixture PRIVATE SceLibKernel_stub SceIofilemgr_stub SceProcessmgr_stub)
    vita_create_self(eboot.bin ww_fixture)
    if(WW_MODE STREQUAL "corpus")
        set(title_id BWWF00001)
    else()
        set(title_id BWWF00002)
    endif()
    vita_create_vpk(ww_${WW_MODE}.vpk ${title_id} eboot.bin VERSION 00.01 NAME "WW ${WW_MODE} fixture")
else()
    enable_testing()
    if(WW_MODE STREQUAL "corpus")
        add_test(NAME upstream_c_execute COMMAND ww_fixture)
    else()
        foreach(scenario shared-alias endian-bulk direct-call indirect-call sustained-stack)
            add_test(NAME ${scenario} COMMAND ww_fixture ${scenario})
        endforeach()
    endif()
endif()
