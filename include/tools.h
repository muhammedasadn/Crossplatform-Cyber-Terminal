#ifndef TOOLS_H
#define TOOLS_H

/*
 * tools.h — Built-in tool launcher for cterm.
 *
 * Ctrl+P opens an overlay showing all registered tools.
 * User can type to filter, Up/Down to navigate, Enter to launch.
 * The list now scrolls so all tools are reachable regardless
 * of window size.
 */

#include <SDL2/SDL.h>
#include <stdarg.h>

/* Maximum tools in the registry */
#define MAX_TOOLS     32

/* Maximum arguments per tool command */
#define MAX_TOOL_ARGS 16

/* How many tool rows are visible at once in the launcher */
#define LAUNCHER_VISIBLE_ROWS 8

/*
 * ToolDef — one registered tool entry.
 *
 * name    — short label shown in the launcher list
 * desc    — one-line description shown to the right
 * command — executable name or full path
 * args[]  — argv array, NULL-terminated, args[0] = command
 * new_tab — always 1 (open in new tab)
 */
typedef struct {
    char  name[32];
    char  desc[80];
    char  command[128];
    char *args[MAX_TOOL_ARGS];
    int   new_tab;
} ToolDef;

/*
 * ToolLauncher — overlay UI state.
 *
 * visible       — 1 = overlay is currently shown
 * selected      — index within the FILTERED list (0-based)
 * scroll_offset — first visible row in the filtered list
 *                 selected is always kept inside the visible window
 * search[]      — text the user has typed to filter tools
 * search_len    — byte length of search string
 */
typedef struct {
    int  visible;
    int  selected;
    int  scroll_offset;   /* ← NEW: top of the visible window */
    char search[64];
    int  search_len;
} ToolLauncher;

/*
 * ToolManager — owns the registry + launcher state.
 */
typedef struct {
    ToolDef      tools[MAX_TOOLS];
    int          count;
    ToolLauncher launcher;
} ToolManager;


/* ── Public API ─────────────────────────────────────────────── */

void tools_init(ToolManager *tm);
void tools_launcher_open(ToolManager *tm);
void tools_launcher_close(ToolManager *tm);

/*
 * tools_launcher_handle_key — process a keypress while open.
 * Returns 1 if the key was consumed.
 *
 * Enter      → launch selected tool
 * Escape     → close
 * Up         → move selection up  (scrolls list if needed)
 * Down       → move selection down (scrolls list if needed)
 * PageUp     → scroll up one full page
 * PageDown   → scroll down one full page
 * Backspace  → delete last search char
 */
int  tools_launcher_handle_key(ToolManager *tm, SDL_Keycode sym,
                                SDL_Keymod mod, void *tabmgr_ptr,
                                int cols, int rows);

/*
 * tools_launcher_handle_text — append typed char to search filter.
 * Resets selection to top when search changes.
 */
void tools_launcher_handle_text(ToolManager *tm, const char *text);

/*
 * tools_launcher_draw — render the launcher overlay.
 * Call once per frame when launcher.visible == 1.
 */
void tools_launcher_draw(ToolManager *tm, SDL_Renderer *renderer,
                         void *font_ptr, int win_w, int win_h);

/*
 * tools_launch — open a new tab and run the tool inside bash.
 */
void tools_launch(ToolDef *tool, void *tabmgr_ptr,
                  int cols, int rows);

#endif /* TOOLS_H */