#ifndef TOOLS_H
#define TOOLS_H

#include <SDL2/SDL.h>

/* Maximum tools in the registry */
#define MAX_TOOLS 32

/* Maximum arguments per tool */
#define MAX_TOOL_ARGS 16

/*
 * ToolDef — describes one registered tool.
 *
 * name     — shown in the launcher list (e.g. "btop")
 * desc     — one-line description shown in the launcher
 * command  — full path or name of the executable
 * args[]   — argv array passed to execvp(), NULL-terminated
 * new_tab  — 1 = open in a new tab, 0 = open in current pane
 */
typedef struct {
    char  name[32];
    char  desc[80];
    char  command[128];
    char *args[MAX_TOOL_ARGS];
    int   new_tab;
} ToolDef;

/*
 * ToolLauncher — the overlay UI state.
 *
 * visible     — 1 = overlay is shown on screen
 * selected    — index of currently highlighted tool
 * search[]    — text the user is typing to filter tools
 * search_len  — length of search string
 */
typedef struct {
    int   visible;
    int   selected;
    char  search[64];
    int   search_len;
} ToolLauncher;

/*
 * ToolManager — owns the registry and launcher state.
 */
typedef struct {
    ToolDef      tools[MAX_TOOLS];
    int          count;
    ToolLauncher launcher;
} ToolManager;

/* ── API ── */

/*
 * tools_init — register all built-in tools.
 * Call once at startup.
 */
void tools_init(ToolManager *tm);

/*
 * tools_launcher_open — show the launcher overlay.
 */
void tools_launcher_open(ToolManager *tm);

/*
 * tools_launcher_close — hide the overlay.
 */
void tools_launcher_close(ToolManager *tm);

/*
 * tools_launcher_handle_key — process a keypress while the
 * launcher is open. Returns 1 if the key was consumed.
 *
 * Enter   → launch selected tool
 * Escape  → close launcher
 * Up/Down → move selection
 * typing  → filter the list
 */
int  tools_launcher_handle_key(ToolManager *tm, SDL_Keycode sym,
                                SDL_Keymod mod, void *tabmgr_ptr,
                                int cols, int rows);

/*
 * tools_launcher_handle_text — append typed character to search.
 */
void tools_launcher_handle_text(ToolManager *tm, const char *text);

/*
 * tools_launcher_draw — render the launcher overlay.
 * Call once per frame when launcher.visible == 1.
 * Drawn on top of everything else.
 */
void tools_launcher_draw(ToolManager *tm, SDL_Renderer *renderer,
                         void *font_ptr, int win_w, int win_h);

/*
 * tools_launch — spawn a tool in a new tab.
 * Uses the existing tab + pane + PTY infrastructure.
 */
void tools_launch(ToolDef *tool, void *tabmgr_ptr,
                  int cols, int rows);

#endif /* TOOLS_H */