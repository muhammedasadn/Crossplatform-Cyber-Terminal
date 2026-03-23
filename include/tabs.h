#ifndef TABS_H
#define TABS_H

#include "ansi.h"
#include "pty.h"
#include <SDL2/SDL.h>

/* Maximum number of tabs allowed open at once */
#define MAX_TABS 16

/* Height of the tab bar in pixels */
#define TAB_BAR_HEIGHT 30

/* Width of each tab button in pixels */
#define TAB_WIDTH 140

/*
 * Tab — one complete terminal session.
 *
 * Each tab owns its own PTY (shell process) and Terminal
 * (cell grid + ANSI parser + scrollback). They are completely
 * independent — closing one doesn't affect others.
 */
typedef struct {
    Terminal *term;         /* Cell grid, parser, scrollback    */
    PTY       pty;          /* Shell process via PTY             */
    char      title[64];    /* Label shown in the tab bar        */
    int       alive;        /* 1 = open, 0 = closed/unused       */
} Tab;

/*
 * TabManager — owns all open tabs and tracks which is active.
 *
 * tabs[]  is a fixed array. We never realloc — we just shift
 * entries left when a tab is closed (simple and safe).
 *
 * active  is the index of the currently focused tab.
 * count   is how many tabs are currently open.
 */
typedef struct {
    Tab  tabs[MAX_TABS];
    int  count;
    int  active;
} TabManager;

/* ── API ── */

int  tabs_init(TabManager *tm, int cols, int rows);
int  tabs_new(TabManager *tm, int cols, int rows);
void tabs_close(TabManager *tm, int i);
void tabs_next(TabManager *tm);
void tabs_prev(TabManager *tm);
void tabs_set_active(TabManager *tm, int i);
Tab *tabs_get_active(TabManager *tm);
void tabs_draw_bar(TabManager *tm, SDL_Renderer *renderer,
                   void *font_ptr, int win_width);
int  tabs_handle_click(TabManager *tm, int mouse_x, int mouse_y,
                       int cols, int rows);
void tabs_destroy(TabManager *tm);

#endif /* TABS_H */