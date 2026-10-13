cmake_minimum_required(VERSION 3.16)
project(ww_armv7_fixture C)
option(WW_IO_PROBE "Host-only injected report I/O failures" OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(BLUEWAKE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(WW_MODE "corpus" CACHE STRING "corpus, contract, fp, arm or mem")
set(GXRUNTIME_DIR "${RECOMPCORE_DIR}/GXRuntime")
set(ABI_DIR "${RECOMPCORE_DIR}/Source/Core/Core/PowerPC/StaticRecomp")
add_subdirectory(diagnostic)
set(required_inputs "${GXRUNTIME_DIR}/include/core/cpu.h")
if(NOT WW_MODE STREQUAL "mem")
    list(APPEND required_inputs "${FIXTURE_DIR}/corpus.c" "${FIXTURE_DIR}/test_c_execute.c"
         "${FIXTURE_DIR}/composite/generated.h")
else()
    list(APPEND required_inputs "${FIXTURE_DIR}/mem_identity.h")
endif()
foreach(required ${required_inputs})
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
elseif(WW_MODE STREQUAL "contract" OR WW_MODE STREQUAL "fp" OR WW_MODE STREQUAL "arm")
    file(GLOB chunks "${FIXTURE_DIR}/composite/chunks_dol/*.c")
    add_executable(ww_fixture ${WW_MODE}_main.c contract_dispatch.c abi.c platform.c
        "${BLUEWAKE_ROOT}/cmake/composite/direct_calls.c" ${chunks})
    target_include_directories(ww_fixture PRIVATE "${FIXTURE_DIR}" "${FIXTURE_DIR}/composite"
        "${BLUEWAKE_ROOT}/cmake/composite" "${ABI_DIR}")
    target_compile_definitions(ww_fixture PRIVATE BLUEWAKE_EDGE_FILTER=1)
    if(WW_MODE STREQUAL "fp" OR WW_MODE STREQUAL "arm")
        target_sources(ww_fixture PRIVATE fp_runtime.c)
    endif()
    if(WW_MODE STREQUAL "arm")
        target_sources(ww_fixture PRIVATE arm_control.c arm_vfp.c)
    endif()
    set_source_files_properties(${chunks} PROPERTIES COMPILE_OPTIONS
        "-include;${BLUEWAKE_ROOT}/cmake/composite/hidden_externs.h")
elseif(WW_MODE STREQUAL "mem")
    add_executable(ww_fixture mem_main.c mem_scenarios.c mem_platform.c abi.c platform.c)
    target_include_directories(ww_fixture PRIVATE "${FIXTURE_DIR}")
else()
    message(FATAL_ERROR "WW_MODE must be corpus, contract, fp, arm or mem")
endif()
target_compile_definitions(ww_fixture PRIVATE DOLRECOMP_CPU_HEADER="core/cpu.h")
target_compile_options(ww_fixture PRIVATE -O2 -ffp-contract=off -fno-fast-math -fstack-usage
    -Wall -Wextra -Werror=implicit-function-declaration -Werror=pointer-to-int-cast
    -Werror=int-to-pointer-cast)
target_link_libraries(ww_fixture PRIVATE ww_cpu m ww_diagnostic_io)
target_link_options(ww_fixture PRIVATE "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/fixture.map")

if(WW_IO_PROBE)
    if(VITA OR NOT WW_MODE STREQUAL "arm")
        message(FATAL_ERROR "WW_IO_PROBE is only for host ARM08 unit tests")
    endif()
    target_sources(ww_fixture PRIVATE mem_io_probe.c arm_io_probe.c)
    target_compile_definitions(ww_fixture PRIVATE DIAGNOSTIC_PREFIX="arm" DIAGNOSTIC_IO_ENV="ARM_IO_FAILURE")
    target_compile_options(ww_fixture PRIVATE -fno-builtin)
    target_link_options(ww_fixture PRIVATE -Wl,--wrap=fprintf -Wl,--wrap=fflush
        -Wl,--wrap=fclose -Wl,--wrap=open -Wl,--wrap=fdopen -Wl,--wrap=arm_vfp_available -Wl,--wrap=arm_vfp_read
        -Wl,--wrap=arm_vfp_write -Wl,--wrap=arm_vfp_operation)
endif()

if(VITA)
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
    foreach(target ww_cpu ww_fixture)
        target_compile_options(${target} PRIVATE -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard)
    endforeach()
    target_link_libraries(ww_fixture PRIVATE SceLibKernel_stub SceIofilemgr_stub SceProcessmgr_stub)
    vita_create_self(eboot.bin ww_fixture)
    if(WW_MODE STREQUAL "corpus")
        set(title_id BWWF00001)
    elseif(WW_MODE STREQUAL "fp")
        set(title_id BWWF00003)
    elseif(WW_MODE STREQUAL "arm")
        set(title_id BWWA00008)
    elseif(WW_MODE STREQUAL "contract")
        set(title_id BWWF00002)
    elseif(WW_MODE STREQUAL "mem")
        set(title_id BWWM00004)
        target_link_libraries(ww_fixture PRIVATE SceSysmem_stub)
    endif()
    vita_create_vpk(ww_${WW_MODE}.vpk ${title_id} eboot.bin VERSION 00.01 NAME "WW ${WW_MODE} fixture")
else()
    enable_testing()
    if(WW_MODE STREQUAL "corpus")
        add_test(NAME upstream_c_execute COMMAND ww_fixture)
    elseif(WW_MODE STREQUAL "contract")
        foreach(scenario shared-alias endian-bulk direct-call indirect-call sustained-stack)
            add_test(NAME ${scenario} COMMAND ww_fixture ${scenario})
        endforeach()
    elseif(WW_MODE STREQUAL "mem")
        add_executable(ww_mem_probe mem_probe.c mem_scenarios.c mem_platform.c)
        target_link_libraries(ww_mem_probe PRIVATE ww_cpu m)
        target_link_options(ww_mem_probe PRIVATE -Wl,--wrap=calloc -Wl,--wrap=free)
        add_test(NAME memory-real-ownership COMMAND ww_mem_probe real)
    elseif(WW_MODE STREQUAL "arm")
        add_executable(ww_arm_control_probe arm_control_probe.c arm_control.c)
        foreach(scenario mapping restore readback-error runtime-observer)
            add_test(NAME arm-${scenario} COMMAND ww_arm_control_probe ${scenario})
        endforeach()
    endif()
endif()
