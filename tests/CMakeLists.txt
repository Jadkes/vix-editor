# tests/CMakeLists.txt - vix test suite
#
# Unit tests run under GoogleTest; the end-to-end test drives the real
# binary through a pty so ncurses rendering paths get exercised too.
# Everything is registered with CTest, so `ctest` (after a build) is the
# single entry point.

cmake_minimum_required(VERSION 3.20)

# GoogleTest is fetched at configure time; the version is pinned so a
# rebuild does not silently pick up a breaking release.
include(FetchContent)
set(GOOGLETEST_VERSION v1.14.0 CACHE STRING "Googletest tag to fetch")
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        ${GOOGLETEST_VERSION}
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # MSVC: keep CRT consistent
FetchContent_MakeAvailable(googletest)

# Each test compiles the sources under test directly rather than linking
# the editor, keeping ncurses and the event loop out of the unit tests.
add_executable(buffer_test buffer_test.cpp ../src/core/buffer.cpp)
target_include_directories(buffer_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(buffer_test PRIVATE GTest::gtest_main)
target_compile_options(buffer_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME BufferTest COMMAND buffer_test)

add_executable(history_test history_test.cpp ../src/history/history.cpp ../src/core/buffer.cpp)
target_include_directories(history_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(history_test PRIVATE GTest::gtest_main)
target_compile_options(history_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME HistoryTest COMMAND history_test)

# End-to-end: run the real vix binary in a pty, feed keystrokes, and check
# the terminal output and exit status. Needs python3 on the machine.
find_program(PYTHON3 python3 REQUIRED)
add_test(
    NAME PtySmoke
    COMMAND ${PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/pty_smoke.py $<TARGET_FILE:vix>
)
