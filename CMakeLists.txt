cmake_minimum_required(VERSION 3.20)
project(vix-editor VERSION 1.0.3 LANGUAGES CXX DESCRIPTION "A vi-style editor in C++")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release when no build type was given and the generator is not
# multi-config (Xcode/Visual Studio pick the config themselves).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# Emit compile_commands.json so clangd and other language servers index correctly.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

# Prefer the wide-char ncursesw build for UTF-8 rendering; if it is missing,
# fall back to plain curses so the project still builds on minimal systems.
set(CURSES_NEED_WIDE TRUE)
find_package(Curses)
if(NOT Curses_FOUND)
    message(WARNING "Wide-char curses (ncursesw) not found; falling back to plain curses")
    unset(CURSES_LIBRARY CACHE)
    unset(CURSES_NCURSES_LIBRARY CACHE)
    unset(CURSES_NCURSESW_LIBRARY CACHE)
    unset(CURSES_INCLUDE_PATH CACHE)
    set(CURSES_NEED_WIDE FALSE)
    find_package(Curses REQUIRED)
endif()

add_executable(vix
    src/main.cpp
    src/editor.cpp
    src/core/buffer.cpp
    src/history/history.cpp
    src/ui/settings.cpp
)

# Modern FindCurses provides the imported Curses::Curses target (includes and
# defines included); fall back to the legacy variables for old toolkits.
if(TARGET Curses::Curses)
    target_link_libraries(vix PRIVATE Curses::Curses)
else()
    target_link_libraries(vix PRIVATE ${CURSES_LIBRARIES})
    target_include_directories(vix PRIVATE ${CURSES_INCLUDE_DIR})
endif()
target_link_libraries(vix PRIVATE Threads::Threads)

target_compile_definitions(vix PRIVATE VIX_VERSION="${PROJECT_VERSION}")

option(VIX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

if(MSVC)
    target_compile_options(vix PRIVATE /W4 /permissive- /utf-8)
    if(VIX_WARNINGS_AS_ERRORS)
        target_compile_options(vix PRIVATE /WX)
    endif()
else()
    target_compile_options(vix PRIVATE -Wall -Wextra -Wpedantic)
    if(VIX_WARNINGS_AS_ERRORS)
        target_compile_options(vix PRIVATE -Werror)
    endif()
endif()

option(VIX_ENABLE_SANITIZERS "Build with ASan+UBSan (GCC/Clang only)" OFF)
if(VIX_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(vix PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
        target_link_options(vix PRIVATE -fsanitize=address,undefined)
    else()
        message(WARNING "VIX_ENABLE_SANITIZERS only supported with GCC/Clang; ignoring")
    endif()
endif()

option(VIX_ENABLE_LTO "Build with link-time optimization where supported" OFF)
if(VIX_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err)
    if(_ipo_ok)
        set_target_properties(vix PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "LTO not supported by this toolchain: ${_ipo_err}")
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS vix RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Tests: unit (GoogleTest) and end-to-end pty smoke tests. Disable with
# -DBUILD_TESTING=OFF to keep the release build free of the gtest dependency.
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
