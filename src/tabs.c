#include "tabs.h"
#include "font.h"
#include "retro_theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int open_tab(TabManager *tm, int i, int cols, int rows) {
    Tab *t = &tm->tabs[i];

    t->root = pane_create_leaf(cols, rows);
    if (!t->root) {
        fprintf(stderr, "open_tab %d: pane_create_leaf failed\n", i);
        return -1;
    }

    t->root->focused = 1;
    snprintf(t->title, sizeof(t->title), "bash [%d]", i + 1);
    t->alive = 1;
    t->has_activity = 0;
    return 0;
}

int tabs_init(TabManager *tm, int cols, int rows) {
    memset(tm, 0, sizeof(TabManager));
    if (open_tab(tm, 0, cols, rows) != 0) {
        fprintf(stderr, "tabs_init: failed to open first tab\n");
        return -1;
    }

    tm->count = 1;
    tm->active = 0;
    return 0;
}

int tabs_new(TabManager *tm, int cols, int rows) {
    if (tm->count >= MAX_TABS) {
        fprintf(stderr, "tabs_new: MAX_TABS (%d) reached\n", MAX_TABS);
        return -1;
    }

    int i = tm->count;
    if (open_tab(tm, i, cols, rows) != 0) {
        return -1;
    }

    tm->count++;
    tm->active = i;
    tm->tabs[i].has_activity = 0;
    return 0;
}

void tabs_close(TabManager *tm, int i) {
    if (i < 0 || i >= tm->count) {
        return;
    }

    if (tm->count <= 1) {
        fprintf(stderr, "tabs_close: refusing to close last tab\n");
        return;
    }

    pane_destroy(tm->tabs[i].root);
    tm->tabs[i].root = NULL;
    tm->tabs[i].alive = 0;

    int tail = tm->count - i - 1;
    if (tail > 0) {
        memmove(&tm->tabs[i], &tm->tabs[i + 1], (size_t)tail * sizeof(Tab));
    }

    memset(&tm->tabs[tm->count - 1], 0, sizeof(Tab));
    tm->count--;
    if (tm->active >= tm->count) {
        tm->active = tm->count - 1;
    }
    if (tm->active >= 0 && tm->active < tm->count) {
        tm->tabs[tm->active].has_activity = 0;
    }
}

void tabs_next(TabManager *tm) {
    tm->active = (tm->active + 1) % tm->count;
    tm->tabs[tm->active].has_activity = 0;
}

void tabs_prev(TabManager *tm) {
    tm->active = (tm->active + tm->count - 1) % tm->count;
    tm->tabs[tm->active].has_activity = 0;
}

void tabs_set_active(TabManager *tm, int i) {
    if (i >= 0 && i < tm->count) {
        tm->active = i;
        tm->tabs[i].has_activity = 0;
    }
}

Tab *tabs_get_active(TabManager *tm) {
    return &tm->tabs[tm->active];
}

void tabs_draw_bar(TabManager *tm, SDL_Renderer *renderer,
                   void *font_ptr, int win_width) {
    Font *font = (Font *)font_ptr;

    SDL_SetRenderDrawColor(renderer, RT_HEADER_R, RT_HEADER_G, RT_HEADER_B, 255);
    SDL_Rect bar_bg = {0, 0, win_width, TAB_BAR_HEIGHT};
    SDL_RenderFillRect(renderer, &bar_bg);

    for (int i = 0; i < tm->count; i++) {
        int x = i * TAB_WIDTH;
        int is_active = (i == tm->active);

        SDL_SetRenderDrawColor(renderer,
                               is_active ? RT_PANEL_R : RT_BG_R,
                               is_active ? RT_PANEL_G : RT_BG_G,
                               is_active ? RT_PANEL_B : RT_BG_B,
                               255);
        SDL_Rect tab_rect = {x + 1, 1, TAB_WIDTH - 2, TAB_BAR_HEIGHT - 2};
        SDL_RenderFillRect(renderer, &tab_rect);

        SDL_SetRenderDrawColor(renderer,
                               RT_BORDER_R, RT_BORDER_G, RT_BORDER_B, 255);
        SDL_RenderDrawRect(renderer, &tab_rect);

        if (is_active) {
            SDL_SetRenderDrawColor(renderer,
                                   RT_ACCENT_R, RT_ACCENT_G, RT_ACCENT_B, 255);
            SDL_Rect accent = {x + 1, 1, TAB_WIDTH - 2, 2};
            SDL_RenderFillRect(renderer, &accent);
        }

        int text_y = (TAB_BAR_HEIGHT - font->cell_height) / 2;
        char display[20];
        snprintf(display, sizeof(display), "%.14s", tm->tabs[i].title);
        font_draw_string(font, renderer, display, x + 8, text_y,
                         is_active ? RT_TEXT_R : RT_DIM_R,
                         is_active ? RT_TEXT_G : RT_DIM_G,
                         is_active ? RT_TEXT_B : RT_DIM_B);

        if (!is_active && tm->tabs[i].has_activity) {
            SDL_SetRenderDrawColor(renderer,
                                   RT_WARN_R, RT_WARN_G, RT_WARN_B, 255);
            SDL_Rect activity = {x + TAB_WIDTH - 26, 8, 6, 6};
            SDL_RenderFillRect(renderer, &activity);
        }

        int close_x = x + TAB_WIDTH - font->cell_width - 8;
        font_draw_char(font, renderer, 'x', close_x, text_y,
                       RT_WARN_R, RT_WARN_G, RT_WARN_B);
    }

    int plus_x = tm->count * TAB_WIDTH;
    if (plus_x + 40 <= win_width) {
        SDL_SetRenderDrawColor(renderer, RT_BG_R, RT_BG_G, RT_BG_B, 255);
        SDL_Rect plus_bg = {plus_x + 1, 1, 38, TAB_BAR_HEIGHT - 2};
        SDL_RenderFillRect(renderer, &plus_bg);
        SDL_SetRenderDrawColor(renderer,
                               RT_BORDER_R, RT_BORDER_G, RT_BORDER_B, 255);
        SDL_RenderDrawRect(renderer, &plus_bg);
        font_draw_char(font, renderer, '+', plus_x + 12,
                       (TAB_BAR_HEIGHT - font->cell_height) / 2,
                       RT_ACCENT_R, RT_ACCENT_G, RT_ACCENT_B);
    }

    SDL_SetRenderDrawColor(renderer, RT_BORDER_R, RT_BORDER_G, RT_BORDER_B, 255);
    SDL_RenderDrawLine(renderer, 0, TAB_BAR_HEIGHT - 1,
                       win_width, TAB_BAR_HEIGHT - 1);
}

int tabs_handle_click(TabManager *tm, int mouse_x, int mouse_y,
                      int cols, int rows) {
    if (mouse_y < 0 || mouse_y >= TAB_BAR_HEIGHT) {
        return 0;
    }

    int plus_x = tm->count * TAB_WIDTH;
    if (mouse_x >= plus_x && mouse_x < plus_x + 40) {
        tabs_new(tm, cols, rows);
        return 1;
    }

    int i = mouse_x / TAB_WIDTH;
    if (i < 0 || i >= tm->count) {
        return 0;
    }

    int close_start = (i * TAB_WIDTH) + TAB_WIDTH - 20;
    if (mouse_x >= close_start) {
        tabs_close(tm, i);
    } else {
        tabs_set_active(tm, i);
    }

    return 1;
}

void tabs_destroy(TabManager *tm) {
    for (int i = 0; i < tm->count; i++) {
        if (tm->tabs[i].alive && tm->tabs[i].root) {
            pane_destroy(tm->tabs[i].root);
        }
        tm->tabs[i].root = NULL;
        tm->tabs[i].alive = 0;
        tm->tabs[i].has_activity = 0;
    }

    tm->count = 0;
    tm->active = 0;
}
