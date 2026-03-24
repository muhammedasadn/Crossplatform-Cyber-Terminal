cmake_minimum_required(VERSION 3.16)
project(cterm C)

# ── C standard ───────────────────────────────────────────────
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Feature-test macros — applied to EVERY source file ───────
#
# add_compile_definitions() passes these as -D flags on the
# compiler command line, which is processed BEFORE any source
# file content. This is the most reliable way to ensure
# _GNU_SOURCE reaches every header on every file.
#
# _GNU_SOURCE     — exposes openpty(), strdup(), getline(), etc.
# _DEFAULT_SOURCE — equivalent on newer glibc (Ubuntu 20.04+)
add_compile_definitions(
    _GNU_SOURCE
    _DEFAULT_SOURCE
)

# ── Include paths ─────────────────────────────────────────────
include_directories(include)

# ── SDL2 ──────────────────────────────────────────────────────
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

# ── FreeType ──────────────────────────────────────────────────
find_package(Freetype REQUIRED)
include_directories(${FREETYPE_INCLUDE_DIRS})

# ── Source files ──────────────────────────────────────────────
#
# Every .c file in the project listed here.
# CMake tracks dependencies and recompiles only changed files.
#
add_executable(cterm
    src/main.c        # Entry point, main loop, event handling
    src/window.c      # SDL2 window + renderer init/destroy
    src/font.c        # FreeType glyph cache + text rendering
    src/pty.c         # PTY open, fork, read, write, resize
    src/ansi.c        # ANSI/VT100 parser + terminal cell grid
    src/tabs.c        # Tab bar + TabManager
    src/pane.c        # Split pane binary tree
    src/tools.c       # Built-in tool launcher overlay
)

# ── Link libraries ────────────────────────────────────────────
#
# SDL2_LIBRARIES     — window, renderer, input events
# FREETYPE_LIBRARIES — font rasterisation
# util               — provides openpty() on Linux
# m                  — math library (FreeType dependency)
#
target_link_libraries(cterm
    ${SDL2_LIBRARIES}
    ${FREETYPE_LIBRARIES}
    util
    m
)

# ── Compiler flags ────────────────────────────────────────────
#
# -Wall              catch common mistakes
# -Wextra            catch extra suspicious code
# -Wno-unused-parameter  suppress "parameter not used" noise
# -g                 embed debug symbols (remove for release)
#
# For a release build, replace -g with -O2 -DNDEBUG
#
target_compile_options(cterm PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -g
)