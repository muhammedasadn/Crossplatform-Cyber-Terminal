#ifndef TABS_H
#define TABS_H

/*
 * tabs.h — Tab management with split-pane support.
 *
 * Each tab owns a root Pane (which may be a single leaf or a
 * full split tree). The TabManager owns all tabs and tracks
 * which one is active.
 */

#include "pane.h"       /* Pane, pane_create_leaf, pane_destroy */
#include <SDL2/SDL.h>

/* Maximum tabs allowed open simultaneously */
#define MAX_TABS        16

/* Height of the tab bar strip at top of window in pixels */
#define TAB_BAR_HEIGHT  30

/* Width of each tab button in pixels */
#define TAB_WIDTH       140


/*
 * Tab — one complete terminal workspace.
 *
 * A tab owns a pane tree rooted at 'root'.
 * When the tab has no splits, root is a single PANE_LEAF.
 * When split, root is a PANE_SPLIT_H or PANE_SPLIT_V node
 * whose leaves each hold independent Terminal + PTY instances.
 */
typedef struct {
    Pane  *root;        /* Root of this tab's pane tree          */
    char   title[64];   /* Label shown in the tab bar            */
    int    alive;       /* 1 = open and valid, 0 = unused slot   */
} Tab;


/*
 * TabManager — owns all open tabs, tracks the active one.
 *
 * tabs[] is a fixed-size array. When a tab is closed we shift
 * remaining entries left to keep the array compact (no holes).
 *
 * active  — index of the currently focused tab (0-based)
 * count   — number of currently open tabs
 */
typedef struct {
    Tab  tabs[MAX_TABS];
    int  count;
    int  active;
} TabManager;


/* ── Public API ─────────────────────────────────────────────── */

/*
 * tabs_init — zero the manager and open the first tab.
 * cols/rows = initial grid size derived from window + font.
 * Returns 0 on success, -1 on failure.
 */
int  tabs_init(TabManager *tm, int cols, int rows);

/*
 * tabs_new — open a new tab at the end of the array.
 * The new tab becomes the active tab.
 * Returns 0 on success, -1 if MAX_TABS is already reached.
 */
int  tabs_new(TabManager *tm, int cols, int rows);

/*
 * tabs_close — close tab at index i.
 * Kills all panes in that tab, shifts remaining tabs left.
 * Refuses to close the last remaining tab.
 */
void tabs_close(TabManager *tm, int i);

/*
 * tabs_next — move focus to the next tab (wraps to 0).
 */
void tabs_next(TabManager *tm);

/*
 * tabs_prev — move focus to the previous tab (wraps to last).
 */
void tabs_prev(TabManager *tm);

/*
 * tabs_set_active — focus a specific tab by index.
 * Silently ignores out-of-range indices.
 */
void tabs_set_active(TabManager *tm, int i);

/*
 * tabs_get_active — returns pointer to the currently active Tab.
 * Always valid as long as count > 0.
 */
Tab *tabs_get_active(TabManager *tm);

/*
 * tabs_draw_bar — render the tab bar strip at the top of the
 * window. Call once per frame before drawing the terminal grid.
 *
 * font_ptr is void* to avoid a circular header dependency;
 * cast it to Font* inside the implementation.
 */
void tabs_draw_bar(TabManager *tm, SDL_Renderer *renderer,
                   void *font_ptr, int win_width);

/*
 * tabs_handle_click — process a left-mouse-button click.
 * If the click landed inside the tab bar, handles it and
 * returns 1 (consumed). Returns 0 if outside the tab bar.
 *
 * Click zones:
 *   Right 20px of a tab  → close that tab
 *   Rest of a tab        → activate that tab
 *   "+" button           → open new tab
 */
int  tabs_handle_click(TabManager *tm, int mouse_x, int mouse_y,
                       int cols, int rows);

/*
 * tabs_destroy — free all open tabs and their pane trees.
 * Call once before exiting the program.
 */
void tabs_destroy(TabManager *tm);


#endif /* TABS_H */