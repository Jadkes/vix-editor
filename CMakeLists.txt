# CMakeLists.txt - Root build for vix-editor
#
# Purpose: Build vix editor binary with C++20
# Dependencies: Python3 (Development), Curses (ncurses)

cmake_minimum_required(VERSION 3.20)
project(vix-editor VERSION 1.0.0 LANGUAGES CXX)

# C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warning flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find dependencies
find_package(Python3 REQUIRED COMPONENTS Development)
find_package(Curses REQUIRED)

# vix editor target
add_executable(vix
    src/vix_editor.cpp
    src/core/buffer.cpp
    src/history/history.cpp
    src/ui/settings.cpp
)
target_link_libraries(vix PRIVATE Python3::Python ${CURSES_LIBRARIES})
target_include_directories(vix PRIVATE ${Python3_INCLUDE_DIRS} ${CURSES_INCLUDE_DIR})

