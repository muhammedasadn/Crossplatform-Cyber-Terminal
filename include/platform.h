#ifndef PLATFORM_H
#define PLATFORM_H

/*
 * platform.h — Cross-platform detection and type definitions.
 *
 * This header is included by every file that needs to know
 * which platform it is compiling for. It sets up:
 *   - CTERM_LINUX or CTERM_WINDOWS
 *   - Consistent path separator
 *   - Shell executable path
 *   - Any platform-specific type aliases
 */

#if defined(_WIN32) || defined(_WIN64)
    #define CTERM_WINDOWS 1

    /* Windows shell */
    #define CTERM_SHELL      "cmd.exe"
    #define CTERM_SHELL_ARG  NULL
    #define PATH_SEP         '\\'

    /* Windows needs these headers for HANDLE, DWORD etc. */
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>

#else
    #define CTERM_LINUX 1

    /* Linux shell */
    #define CTERM_SHELL      "/bin/bash"
    #define CTERM_SHELL_ARG  "-bash"
    #define PATH_SEP         '/'

#endif

#endif /* PLATFORM_H */