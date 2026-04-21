# tests/CMakeLists.txt - GoogleTest framework for vix-editor
#
# Purpose: Set up C++ unit testing with GoogleTest
# Design: Uses FetchContent to download GoogleTest at configure time

cmake_minimum_required(VERSION 3.20)
project(vix_editor_tests)

# C++17 standard for tests
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Download and configure GoogleTest
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

# For Windows: Prevent overriding the parent project's compiler settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()

# Define test executable
add_executable(buffer_test buffer_test.cpp)
target_link_libraries(buffer_test PRIVATE GTest::gtest_main)
target_include_directories(buffer_test PRIVATE
    ${CMAKE_SOURCE_DIR}/../src
)
target_compile_options(buffer_test PRIVATE -Wall -Wextra)

add_executable(history_test history_test.cpp ../src/history/history.cpp)
target_link_libraries(history_test PRIVATE GTest::gtest_main)
target_include_directories(history_test PRIVATE
    ${CMAKE_SOURCE_DIR}/../src
)
target_compile_options(history_test PRIVATE -Wall -Wextra)

# Register tests with CTest
add_test(NAME BufferTest COMMAND buffer_test)
add_test(NAME HistoryTest COMMAND history_test)
