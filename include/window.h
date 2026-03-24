#include "platform.h"
#ifndef WINDOW_H
#define WINDOW_H

/* SDL2 gives us the window, renderer, and event system */
#include <SDL2/SDL.h>

/*
 * This struct holds everything related to our window.
 * A struct in C is a way to group related variables together.
 * Think of it as a "bundle" of data.
 */
typedef struct {
    SDL_Window   *window;    /* The OS window itself          */
    SDL_Renderer *renderer;  /* The GPU renderer for drawing  */
    int           width;     /* Window width in pixels        */
    int           height;    /* Window height in pixels       */
    int           running;   /* 1 = keep looping, 0 = quit    */
} Window;

/* Function declarations — these say "these functions exist" */
/* The actual code for them is in window.c                   */
int  window_init(Window *w, int width, int height);
void window_handle_events(Window *w);
void window_render_begin(Window *w);
void window_render_end(Window *w);
void window_destroy(Window *w);

#endif /* WINDOW_H */