cmake_minimum_required(VERSION 3.16)
project(cterm C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(include)

# Ask pkg-config where SDL2 is installed on this system
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

add_executable(cterm
    src/main.c
    src/window.c
)

# Link against SDL2 — this tells the linker to include SDL2's code
target_link_libraries(cterm ${SDL2_LIBRARIES})