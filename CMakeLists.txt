cmake_minimum_required(VERSION 3.16)
project(cterm C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Platform ─────────────────────────────────────────────────
if(WIN32)
    message(STATUS "Building for Windows")
    add_compile_definitions(CTERM_WINDOWS _WIN32_WINNT=0x0A00)
else()
    message(STATUS "Building for Linux")
    add_compile_definitions(
        CTERM_LINUX
        _GNU_SOURCE
        _DEFAULT_SOURCE
    )
endif()

include_directories(include)

# ── SDL2 ──────────────────────────────────────────────────────
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

# ── FreeType ──────────────────────────────────────────────────
find_package(Freetype REQUIRED)
include_directories(${FREETYPE_INCLUDE_DIRS})

# ── Sources ───────────────────────────────────────────────────
add_executable(cterm
    src/main.c       # Entry point, main loop, event handling
    src/window.c     # SDL2 window + renderer + fullscreen
    src/font.c       # FreeType glyph cache + text rendering
    src/pty.c        # PTY open, fork, read, write, resize
    src/ansi.c       # ANSI/VT100 parser + terminal grid
    src/tabs.c       # Tab bar + TabManager
    src/pane.c       # Split pane binary tree
    src/tools.c      # Built-in tool launcher overlay
    src/config.c     # Config file loader (Module 12)
)

# ── Link ──────────────────────────────────────────────────────
if(WIN32)
    target_link_libraries(cterm
        ${SDL2_LIBRARIES}
        ${FREETYPE_LIBRARIES}
        kernel32 user32 gdi32 m
    )
else()
    target_link_libraries(cterm
        ${SDL2_LIBRARIES}
        ${FREETYPE_LIBRARIES}
        util    # openpty()
        m       # math (FreeType)
    )
endif()

# ── Compiler flags ────────────────────────────────────────────
target_compile_options(cterm PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-implicit-function-declaration
    -g
)

target_compile_options(cterm PRIVATE
    -O2
    -D_FORTIFY_SOURCE=2
    -fstack-protector-strong
    -Wformat
    -Wformat-security
)
target_link_options(cterm PRIVATE
    -Wl,-z,relro
    -Wl,-z,now
)