cmake_minimum_required(VERSION 3.16)
project(remote_hid_unit_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(keyboard_core_tests
    keyboard_core_tests.cpp
    ../../components/keyboard_core/src/key_map.cpp
    ../../components/keyboard_core/src/keyboard_engine.cpp
)
target_include_directories(keyboard_core_tests PRIVATE
    ../../components/keyboard_core/include
)
target_compile_options(keyboard_core_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)

enable_testing()
add_test(NAME keyboard_core_tests COMMAND keyboard_core_tests)
