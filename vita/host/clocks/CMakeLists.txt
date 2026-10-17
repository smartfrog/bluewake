cmake_minimum_required(VERSION 3.16)
project(ww13_clocks C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(ww13_clocks STATIC vita_clocks.c)
target_include_directories(ww13_clocks PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ww13_clocks PRIVATE -Wall -Wextra -Werror)

option(WW13_TESTS_ONLY "Build game-free host tests only" ON)
if(WW13_TESTS_ONLY)
    enable_testing()
    add_executable(vita_clocks_test vita_clocks_test.c)
    target_link_libraries(vita_clocks_test PRIVATE ww13_clocks)
    target_compile_options(vita_clocks_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME vita_clocks_cases COMMAND vita_clocks_test all)
endif()
