cmake_minimum_required(VERSION 3.16)
project(cterm C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(include)

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

find_package(Freetype REQUIRED)
include_directories(${FREETYPE_INCLUDE_DIRS})

add_executable(cterm
    src/main.c
    src/window.c
    src/font.c
    src/pty.c
    src/ansi.c
    src/tabs.c
)

target_link_libraries(cterm
    ${SDL2_LIBRARIES}
    ${FREETYPE_LIBRARIES}
    util
    m
)