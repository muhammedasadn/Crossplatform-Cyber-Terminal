cmake_minimum_required(VERSION 3.16)
project(cterm C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Platform detection ────────────────────────────────────────
if(WIN32)
    message(STATUS "cterm: building for Windows")
    add_compile_definitions(CTERM_WINDOWS _WIN32_WINNT=0x0A00)
else()
    message(STATUS "cterm: building for Linux")
    add_compile_definitions(CTERM_LINUX _GNU_SOURCE _DEFAULT_SOURCE)
endif()

# ── Include paths ─────────────────────────────────────────────
include_directories(include)

# ── SDL2 ──────────────────────────────────────────────────────
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

# ── FreeType ──────────────────────────────────────────────────
find_package(Freetype REQUIRED)
include_directories(${FREETYPE_INCLUDE_DIRS})

# ── Source files ──────────────────────────────────────────────
add_executable(cterm
    src/main.c       # Entry point, main loop, event handling
    src/window.c     # SDL2 window + renderer
    src/font.c       # FreeType glyph cache + rendering
    src/pty.c        # PTY: fork, exec, read, write, resize
    src/ansi.c       # ANSI/VT100 parser + terminal cell grid
    src/tabs.c       # Tab bar + TabManager
    src/pane.c       # Split pane binary tree
    src/tools.c      # Built-in tool launcher
    src/config.c     # Config file loader (Module 12)
)

# ── Link libraries ────────────────────────────────────────────
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

# ── Install target (optional) ─────────────────────────────────
# Run: sudo cmake --install build
install(TARGETS cterm DESTINATION /usr/local/bin)
install(DIRECTORY assets/ DESTINATION /usr/local/share/cterm)