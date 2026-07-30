cmake_minimum_required(VERSION 3.20)
project(vix-editor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Curses REQUIRED)

add_executable(vix
    src/main.cpp
    src/editor.cpp
    src/core/buffer.cpp
    src/history/history.cpp
    src/ui/settings.cpp
)

target_link_libraries(vix PRIVATE ${CURSES_LIBRARIES})
target_include_directories(vix PRIVATE ${CURSES_INCLUDE_DIR})

install(TARGETS vix RUNTIME DESTINATION bin)
