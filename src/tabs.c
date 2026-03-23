/*
 * tabs.c — Tab management implementation.
 *
 * Each tab is an independent shell session with its own
 * PTY process and Terminal grid. The TabManager owns all
 * tabs and tracks which one is currently focused.
 */

#include "tabs.h"
#include "font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ── Internal helper ────────────────────────────────────────── */

/*
 * open_tab — allocates and starts one tab at index i.
 * Creates a fresh Terminal and spawns a new PTY/bash.
 */
static int open_tab(TabManager *tm, int i, int cols, int rows) {
    Tab *t = &tm->tabs[i];

    /* Create the terminal grid + parser */
    t->term = terminal_create(cols, rows);
    if (!t->term) {
        fprintf(stderr, "tab %d: terminal_create failed\n", i);
        return -1;
    }

    /* Spawn the shell process */
    if (pty_init(&t->pty, cols, rows) != 0) {
        fprintf(stderr, "tab %d: pty_init failed\n", i);
        terminal_destroy(t->term);
        t->term = NULL;
        return -1;
    }

    /* Label shown in the tab bar */
    snprintf(t->title, sizeof(t->title), "bash [%d]", i + 1);
    t->alive = 1;

    return 0;
}


/* ── tabs_init ──────────────────────────────────────────────── */

int tabs_init(TabManager *tm, int cols, int rows) {
    /*
     * Zero the entire struct so all pointers start NULL
     * and alive flags start 0. calloc/memset both work here.
     */
    memset(tm, 0, sizeof(TabManager));
    tm->count  = 0;
    tm->active = 0;

    if (open_tab(tm, 0, cols, rows) != 0) return -1;
    tm->count = 1;

    printf("TabManager initialized, first tab opened.\n");
    return 0;
}


/* ── tabs_new ───────────────────────────────────────────────── */

int tabs_new(TabManager *tm, int cols, int rows) {
    if (tm->count >= MAX_TABS) {
        fprintf(stderr, "tabs_new: MAX_TABS (%d) reached\n", MAX_TABS);
        return -1;
    }

    int i = tm->count;

    if (open_tab(tm, i, cols, rows) != 0) return -1;

    tm->count++;
    tm->active = i;  /* Focus the newly opened tab */

    printf("Opened tab %d (total: %d)\n", i + 1, tm->count);
    return 0;
}


/* ── tabs_close ─────────────────────────────────────────────── */

void tabs_close(TabManager *tm, int i) {
    if (i < 0 || i >= tm->count) return;

    /* Never close the last tab — would leave nothing to render */
    if (tm->count <= 1) {
        fprintf(stderr, "tabs_close: refusing to close last tab\n");
        return;
    }

    Tab *t = &tm->tabs[i];

    /* Shut down the shell and free the terminal grid */
    pty_destroy(&t->pty);
    terminal_destroy(t->term);
    t->term  = NULL;
    t->alive = 0;

    /*
     * Shift all tabs after position i one step to the left.
     * This keeps the array compact — no gaps, no holes.
     * memmove handles overlapping memory regions safely.
     */
    int remaining = tm->count - i - 1;
    if (remaining > 0) {
        memmove(&tm->tabs[i], &tm->tabs[i + 1],
                remaining * sizeof(Tab));
    }

    /* Zero the now-empty last slot */
    memset(&tm->tabs[tm->count - 1], 0, sizeof(Tab));
    tm->count--;

    /* Keep active index in valid range */
    if (tm->active >= tm->count) {
        tm->active = tm->count - 1;
    }

    printf("Closed tab %d (remaining: %d)\n", i + 1, tm->count);
}


/* ── tabs_next / tabs_prev ──────────────────────────────────── */

void tabs_next(TabManager *tm) {
    /* % operator gives wraparound: last+1 wraps to 0 */
    tm->active = (tm->active + 1) % tm->count;
}

void tabs_prev(TabManager *tm) {
    /* Adding count prevents negative: 0-1+count = count-1 */
    tm->active = (tm->active + tm->count - 1) % tm->count;
}


/* ── tabs_set_active ────────────────────────────────────────── */

void tabs_set_active(TabManager *tm, int i) {
    if (i >= 0 && i < tm->count) {
        tm->active = i;
    }
}


/* ── tabs_get_active ────────────────────────────────────────── */

Tab *tabs_get_active(TabManager *tm) {
    return &tm->tabs[tm->active];
}


/* ── tabs_draw_bar ──────────────────────────────────────────── */

/*
 * Renders the tab bar strip at the very top of the window.
 *
 * Layout:
 *   [tab 0][tab 1][tab 2]...[+]
 *
 * Active tab: lighter background + blue top accent line.
 * Inactive tabs: darker background, dimmer text.
 * "+" button: opens a new tab.
 * "x" button: appears at right of each tab, closes it.
 */
void tabs_draw_bar(TabManager *tm, SDL_Renderer *renderer,
                   void *font_ptr, int win_width) {
    Font *font = (Font *)font_ptr;

    /* Tab bar background */
    SDL_SetRenderDrawColor(renderer, 22, 22, 22, 255);
    SDL_Rect bar_bg = {0, 0, win_width, TAB_BAR_HEIGHT};
    SDL_RenderFillRect(renderer, &bar_bg);

    /* Draw each tab */
    for (int i = 0; i < tm->count; i++) {
        int x         = i * TAB_WIDTH;
        int is_active = (i == tm->active);

        /* Tab background fill */
        if (is_active) {
            SDL_SetRenderDrawColor(renderer, 48, 48, 48, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 28, 28, 28, 255);
        }
        SDL_Rect tab_rect = {x + 1, 1, TAB_WIDTH - 2, TAB_BAR_HEIGHT - 1};
        SDL_RenderFillRect(renderer, &tab_rect);

        /* Blue top accent line on active tab */
        if (is_active) {
            SDL_SetRenderDrawColor(renderer, 80, 160, 255, 255);
            SDL_Rect accent = {x + 1, 1, TAB_WIDTH - 2, 2};
            SDL_RenderFillRect(renderer, &accent);
        }

        /* Tab title text color: bright if active, dim otherwise */
        Uint8 tr = is_active ? 220 : 120;
        Uint8 tg = is_active ? 220 : 120;
        Uint8 tb = is_active ? 220 : 120;

        /* Vertically center text in the bar */
        int text_y = (TAB_BAR_HEIGHT - font->cell_height) / 2;
        int text_x = x + 8;

        /* Draw title, capped to 14 chars to fit the tab */
        char display[20];
        snprintf(display, sizeof(display), "%.14s", tm->tabs[i].title);
        font_draw_string(font, renderer, display,
                         text_x, text_y, tr, tg, tb);

        /* Close button "x" at right side of tab */
        int close_x = x + TAB_WIDTH - font->cell_width - 8;
        font_draw_char(font, renderer, 'x',
                       close_x, text_y, 160, 70, 70);

        /* Vertical separator between tabs */
        SDL_SetRenderDrawColor(renderer, 55, 55, 55, 255);
        SDL_RenderDrawLine(renderer,
                           x + TAB_WIDTH - 1, 3,
                           x + TAB_WIDTH - 1, TAB_BAR_HEIGHT - 3);
    }

    /* "+" new tab button — appears right after the last tab */
    int plus_x = tm->count * TAB_WIDTH;
    if (plus_x + 40 <= win_width) {
        SDL_SetRenderDrawColor(renderer, 33, 33, 33, 255);
        SDL_Rect plus_bg = {plus_x + 1, 1, 38, TAB_BAR_HEIGHT - 1};
        SDL_RenderFillRect(renderer, &plus_bg);

        font_draw_char(font, renderer, '+',
                       plus_x + 12,
                       (TAB_BAR_HEIGHT - font->cell_height) / 2,
                       90, 190, 90);
    }

    /* Bottom border line separating tab bar from terminal area */
    SDL_SetRenderDrawColor(renderer, 55, 55, 55, 255);
    SDL_RenderDrawLine(renderer,
                       0, TAB_BAR_HEIGHT - 1,
                       win_width, TAB_BAR_HEIGHT - 1);
}


/* ── tabs_handle_click ──────────────────────────────────────── */

/*
 * Handles a mouse click that landed in the tab bar area.
 * Returns 1 if the click was consumed (don't pass to terminal).
 * Returns 0 if the click was outside the tab bar.
 *
 * Click zones:
 *   Rightmost 20px of a tab  → close that tab
 *   Rest of a tab            → activate that tab
 *   "+" button area          → open new tab
 */
int tabs_handle_click(TabManager *tm, int mouse_x, int mouse_y,
                      int cols, int rows) {
    /* Only handle clicks within the tab bar */
    if (mouse_y < 0 || mouse_y >= TAB_BAR_HEIGHT) return 0;

    /* Check "+" new tab button */
    int plus_x = tm->count * TAB_WIDTH;
    if (mouse_x >= plus_x && mouse_x < plus_x + 40) {
        tabs_new(tm, cols, rows);
        return 1;
    }

    /* Which tab index was clicked? */
    int i = mouse_x / TAB_WIDTH;
    if (i < 0 || i >= tm->count) return 0;

    /* Close button = rightmost 20px of the tab */
    int close_start = (i * TAB_WIDTH) + TAB_WIDTH - 20;
    if (mouse_x >= close_start) {
        tabs_close(tm, i);
    } else {
        tabs_set_active(tm, i);
    }

    return 1;
}


/* ── tabs_destroy ───────────────────────────────────────────── */

/*
 * Frees all open tabs in order.
 * Always call this before exiting the program.
 */
void tabs_destroy(TabManager *tm) {
    for (int i = 0; i < tm->count; i++) {
        if (tm->tabs[i].alive) {
            pty_destroy(&tm->tabs[i].pty);
            terminal_destroy(tm->tabs[i].term);
            tm->tabs[i].term  = NULL;
            tm->tabs[i].alive = 0;
        }
    }
    tm->count  = 0;
    tm->active = 0;
    printf("All tabs destroyed.\n");
}