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
vix_compile_options(buffer_test)
vix_enable_sanitizers(buffer_test)
add_test(NAME BufferTest COMMAND buffer_test)
set_tests_properties(BufferTest PROPERTIES TIMEOUT 60)

add_executable(history_test history_test.cpp ../src/history/history.cpp ../src/core/buffer.cpp)
target_include_directories(history_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(history_test PRIVATE GTest::gtest_main)
vix_compile_options(history_test)
vix_enable_sanitizers(history_test)
add_test(NAME HistoryTest COMMAND history_test)
set_tests_properties(HistoryTest PROPERTIES TIMEOUT 60)

# End-to-end: run the real vix binary in a pty, feed keystrokes, and check
# the terminal output and exit status. Optional so a machine without python3
# (or a non-POSIX host without pty support) can still build and run the unit
# tests; the smoke test simply is not registered then.
find_program(PYTHON3 python3)
if(PYTHON3 AND NOT WIN32)
    add_test(
        NAME PtySmoke
        COMMAND ${PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/pty_smoke.py $<TARGET_FILE:vix>
    )
    # The pty scenarios bound their own waits at ~8s each; this is a backstop
    # so a hung editor kills the test instead of stalling the whole job.
    set_tests_properties(PtySmoke PROPERTIES TIMEOUT 180)
endif()
