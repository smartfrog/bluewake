cmake_minimum_required(VERSION 3.16)
project(ww13_dvd C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(WW13_TESTS_ONLY "Build game-free host tests only" ON)

add_library(ww13_dvd_io STATIC dvd_io.c)
target_include_directories(ww13_dvd_io PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ww13_dvd_io PRIVATE -Wall -Wextra -Werror)

if(WW13_TESTS_ONLY)
    enable_testing()
    add_executable(dvd_io_test dvd_io_test.c)
    target_link_libraries(dvd_io_test PRIVATE ww13_dvd_io)
    target_compile_options(dvd_io_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME dvd_io_cases COMMAND dvd_io_test all)
endif()
