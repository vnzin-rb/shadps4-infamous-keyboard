# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# Also usable independently: cmake -S tests/touchpad -B build-touchpad
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.24)
    project(shadps4_touchpad_regression LANGUAGES CXX)
    enable_testing()
endif()

get_filename_component(SHAD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(shadps4_touchpad_test
    test_touchpad.cpp
    test_support.cpp
    ${SHAD_ROOT}/src/input/controller.cpp
    ${SHAD_ROOT}/src/input/controller_state.cpp
    ${SHAD_ROOT}/src/input/input_handler.cpp
    ${SHAD_ROOT}/src/input/input_mouse.cpp
    ${SHAD_ROOT}/src/core/libraries/pad/pad.cpp
)
target_compile_features(shadps4_touchpad_test PRIVATE cxx_std_23)
target_compile_definitions(shadps4_touchpad_test PRIVATE FMT_HEADER_ONLY)
target_compile_definitions(shadps4_touchpad_test PRIVATE
    TEST_USER_DIR="${CMAKE_CURRENT_BINARY_DIR}/isolated-user")
file(COPY ${SHAD_ROOT}/package/spray-profile/input_config
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/isolated-user)
target_include_directories(shadps4_touchpad_test BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${SHAD_ROOT}/src
    ${SHAD_ROOT}/externals/sdl3/include
    ${SHAD_ROOT}/externals/fmt/include
    ${SHAD_ROOT}/externals/json/include
)
# Real controller, keyboard bindings and guest report packing are compiled. Game metadata,
# GUI, clock and hardware dependencies use test doubles; settings stay in memory.
add_test(NAME TouchpadRegression COMMAND shadps4_touchpad_test)
set_tests_properties(TouchpadRegression PROPERTIES TIMEOUT 30)
