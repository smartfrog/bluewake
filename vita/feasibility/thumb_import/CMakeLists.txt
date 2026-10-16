cmake_minimum_required(VERSION 3.16)
project(public_thumb_import C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT VITA)
    message(FATAL_ERROR "Use the existing Vita SDK toolchain; host tests use test_probe.py")
endif()
add_executable(public_thumb_import core.c copy.c vita_main.c)
target_compile_options(public_thumb_import PRIVATE
    -Os -Wall -Wextra -Werror -mthumb -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -fno-lto)
set_source_files_properties(copy.c PROPERTIES COMPILE_OPTIONS
    "-fno-builtin-memcpy;-fno-optimize-sibling-calls")
target_link_options(public_thumb_import PRIVATE -fno-lto
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/public_thumb_import.map")
target_link_libraries(public_thumb_import PRIVATE
    SceLibKernel_stub SceIofilemgr_stub SceProcessmgr_stub)
