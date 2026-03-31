#include "window.h"
#include <stdio.h>
#include <string.h>

int window_init(Window *w, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return -1;
    }
    w->width = width; w->height = height;
    w->running = 1; w->fullscreen = 0;
    memset(&w->sel, 0, sizeof(Selection));

    w->window = SDL_CreateWindow("cterm",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!w->window) { fprintf(stderr,"Window: %s\n",SDL_GetError()); return -1; }
    SDL_SetWindowMinimumSize(w->window, 400, 200);

    w->renderer = SDL_CreateRenderer(w->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!w->renderer) {
        fprintf(stderr,"Renderer: %s\n",SDL_GetError());
        SDL_DestroyWindow(w->window); return -1;
    }
    SDL_SetRenderDrawBlendMode(w->renderer, SDL_BLENDMODE_BLEND);
    return 0;
}

void window_toggle_fullscreen(Window *w) {
    w->fullscreen = !w->fullscreen;
    SDL_SetWindowFullscreen(w->window,
        w->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_GetWindowSize(w->window, &w->width, &w->height);
}

void window_render_begin(Window *w) {
    SDL_SetRenderDrawColor(w->renderer, RT_BG_R, RT_BG_G, RT_BG_B, 255);
    SDL_RenderClear(w->renderer);
}

void window_render_end(Window *w) { SDL_RenderPresent(w->renderer); }

void window_destroy(Window *w) {
    if (w->renderer) SDL_DestroyRenderer(w->renderer);
    if (w->window)   SDL_DestroyWindow(w->window);
    SDL_Quit();
}

void clipboard_copy(const char *text) {
    if (text && *text) SDL_SetClipboardText(text);
}

char *clipboard_paste(void) {
    if (!SDL_HasClipboardText()) return NULL;
    return SDL_GetClipboardText();
}