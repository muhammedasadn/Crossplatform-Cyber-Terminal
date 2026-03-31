#ifndef TABS_H
#define TABS_H

#include "pane.h"
#include <SDL2/SDL.h>

#define MAX_TABS        16
#define TAB_BAR_HEIGHT  30
#define TAB_WIDTH       140

typedef struct {
    Pane *root;
    char  title[64];
    int   alive;
    int   has_activity;
} Tab;

typedef struct {
    Tab tabs[MAX_TABS];
    int count;
    int active;
} TabManager;

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

#endif
