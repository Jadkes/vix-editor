cmake_minimum_required(VERSION 3.20)
project(vix-editor VERSION 1.1.0 LANGUAGES CXX DESCRIPTION "A vi-style editor in C++")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release when no build type was given and the generator is not
# multi-config (Xcode/Visual Studio pick the config themselves).
get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT is_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# Emit compile_commands.json so clangd and other language servers index correctly.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# vix renders UTF-8 through wcwidth/curses wide chars, so ncursesw is a hard
# dependency. CURSES_NEED_WIDE makes FindCurses search the ncursesw library
# family; CURSES_NEED_NCURSES rejects falling back to plain libcurses.
set(CURSES_NEED_WIDE TRUE)
set(CURSES_NEED_NCURSES TRUE)
find_package(Curses REQUIRED)

add_executable(vix
    src/main.cpp
    src/editor.cpp
    src/core/buffer.cpp
    src/history/history.cpp
    src/ui/settings.cpp
)

# FindCurses has never provided a Curses::Curses target (the docs show how to
# create one); build the imported target ourselves so linking is one code path.
if(NOT TARGET Curses::Curses)
    add_library(Curses::Curses INTERFACE IMPORTED)
    set_target_properties(Curses::Curses PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CURSES_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${CURSES_LIBRARIES}"
    )
endif()
target_link_libraries(vix PRIVATE Curses::Curses)

target_compile_definitions(vix PRIVATE VIX_VERSION="${PROJECT_VERSION}")

option(VIX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Shared warning flags for the editor and the test binaries, so the test
# targets get the same -Wall/-Werror treatment (and -Werror when configured).
function(vix_compile_options TARGET)
    if(MSVC)
        target_compile_options(${TARGET} PRIVATE /W4 /permissive- /utf-8)
        if(VIX_WARNINGS_AS_ERRORS)
            target_compile_options(${TARGET} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Wpedantic)
        if(VIX_WARNINGS_AS_ERRORS)
            target_compile_options(${TARGET} PRIVATE -Werror)
        endif()
    endif()
endfunction()

vix_compile_options(vix)

option(VIX_ENABLE_SANITIZERS "Build with ASan+UBSan (GCC/Clang only)" OFF)
# Applied to the editor and every test binary (buffer/history are precisely
# where the OOB/leak class bugs that ASan catches live). No-op unless the
# option is on and the compiler supports the flags.
function(vix_enable_sanitizers TARGET)
    if(VIX_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${TARGET} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${TARGET} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()
vix_enable_sanitizers(vix)
if(VIX_ENABLE_SANITIZERS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    message(WARNING "VIX_ENABLE_SANITIZERS only supported with GCC/Clang; ignoring")
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

# Tests: unit (GoogleTest) and end-to-end pty smoke tests. include(CTest)
# defines BUILD_TESTING, ON by default; turn it off with -DBUILD_TESTING=OFF
# (as the build-only preset does) to keep the binary free of gtest.
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
