#ifndef WINDOW_H
#define WINDOW_H

#include <SDL2/SDL.h>
#include "retro_theme.h"

typedef struct {
    int active;
    int start_col, start_row;
    int end_col,   end_row;
    int has_selection;
} Selection;

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    int           width;
    int           height;
    int           running;
    int           fullscreen;
    Selection     sel;
} Window;

int  window_init(Window *w, int width, int height);
void window_toggle_fullscreen(Window *w);
void window_render_begin(Window *w);
void window_render_end(Window *w);
void window_destroy(Window *w);
void clipboard_copy(const char *text);
char *clipboard_paste(void);

#endif