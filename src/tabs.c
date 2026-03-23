/*
 * tabs.c — Tab management implementation.
 *
 * Each tab owns a pane tree. Opening a tab creates one leaf
 * pane (single shell). Splitting panes happens via pane.c.
 * Closing a tab destroys the entire pane tree.
 */

#include "tabs.h"
#include "font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ── Internal: open_tab ─────────────────────────────────────── */

/*
 * Initialises slot i in the tabs array.
 * Creates one PANE_LEAF with its own Terminal and PTY.
 * Sets alive=1 and writes a title string.
 */
static int open_tab(TabManager *tm, int i, int cols, int rows) {
    Tab *t = &tm->tabs[i];

    t->root = pane_create_leaf(cols, rows);
    if (!t->root) {
        fprintf(stderr, "open_tab %d: pane_create_leaf failed\n", i);
        return -1;
    }

    /* The root leaf starts focused */
    t->root->focused = 1;

    snprintf(t->title, sizeof(t->title), "bash [%d]", i + 1);
    t->alive = 1;

    return 0;
}


/* ── tabs_init ──────────────────────────────────────────────── */

int tabs_init(TabManager *tm, int cols, int rows) {
    /*
     * Zero the whole struct first.
     * This ensures all Tab.root pointers start NULL and
     * all alive flags start 0, which matters for
     * safe iteration in tabs_destroy.
     */
    memset(tm, 0, sizeof(TabManager));
    tm->count  = 0;
    tm->active = 0;

    if (open_tab(tm, 0, cols, rows) != 0) {
        fprintf(stderr, "tabs_init: failed to open first tab\n");
        return -1;
    }

    tm->count = 1;
    printf("TabManager ready. First tab opened.\n");
    return 0;
}


/* ── tabs_new ───────────────────────────────────────────────── */

int tabs_new(TabManager *tm, int cols, int rows) {
    if (tm->count >= MAX_TABS) {
        fprintf(stderr, "tabs_new: MAX_TABS (%d) reached\n", MAX_TABS);
        return -1;
    }

    int i = tm->count;   /* New tab goes at the end */

    if (open_tab(tm, i, cols, rows) != 0) return -1;

    tm->count++;
    tm->active = i;      /* Switch focus to the new tab */

    printf("Tab %d opened (total: %d)\n", i + 1, tm->count);
    return 0;
}


/* ── tabs_close ─────────────────────────────────────────────── */

void tabs_close(TabManager *tm, int i) {
    if (i < 0 || i >= tm->count) return;

    /* Never close the last remaining tab */
    if (tm->count <= 1) {
        fprintf(stderr, "tabs_close: refusing to close last tab\n");
        return;
    }

    Tab *t = &tm->tabs[i];

    /* Destroy the entire pane tree for this tab */
    pane_destroy(t->root);
    t->root  = NULL;
    t->alive = 0;

    /*
     * Shift all tabs after position i one place to the left.
     * Keeps the array compact with no empty holes.
     * memmove is safe for overlapping regions.
     */
    int tail = tm->count - i - 1;
    if (tail > 0) {
        memmove(&tm->tabs[i], &tm->tabs[i + 1],
                tail * sizeof(Tab));
    }

    /* Zero the vacated last slot */
    memset(&tm->tabs[tm->count - 1], 0, sizeof(Tab));
    tm->count--;

    /* Clamp active index to valid range */
    if (tm->active >= tm->count)
        tm->active = tm->count - 1;

    printf("Tab %d closed (remaining: %d)\n", i + 1, tm->count);
}


/* ── tabs_next / tabs_prev ──────────────────────────────────── */

void tabs_next(TabManager *tm) {
    /* (n + 1) % count wraps last → 0 */
    tm->active = (tm->active + 1) % tm->count;
}

void tabs_prev(TabManager *tm) {
    /* (n + count - 1) % count wraps 0 → last */
    tm->active = (tm->active + tm->count - 1) % tm->count;
}


/* ── tabs_set_active ────────────────────────────────────────── */

void tabs_set_active(TabManager *tm, int i) {
    if (i >= 0 && i < tm->count)
        tm->active = i;
}


/* ── tabs_get_active ────────────────────────────────────────── */

Tab *tabs_get_active(TabManager *tm) {
    return &tm->tabs[tm->active];
}


/* ── tabs_draw_bar ──────────────────────────────────────────── */

/*
 * Renders the tab bar at the very top of the window.
 *
 * Visual layout (left to right):
 *   [tab 0 title  x][tab 1 title  x][tab 2 title  x][+]
 *
 * Active tab:   lighter background + blue top accent line.
 * Inactive tab: darker background, dimmer text.
 * Close button: "x" near the right edge of each tab.
 * New tab:      "+" button after the last tab.
 * Bottom border: thin separator line.
 */
void tabs_draw_bar(TabManager *tm, SDL_Renderer *renderer,
                   void *font_ptr, int win_width) {
    Font *font = (Font *)font_ptr;

    /* ── Background of the entire bar ── */
    SDL_SetRenderDrawColor(renderer, 22, 22, 22, 255);
    SDL_Rect bar_bg = {0, 0, win_width, TAB_BAR_HEIGHT};
    SDL_RenderFillRect(renderer, &bar_bg);

    /* ── Each tab button ── */
    for (int i = 0; i < tm->count; i++) {
        int x         = i * TAB_WIDTH;
        int is_active = (i == tm->active);

        /* Tab background */
        SDL_SetRenderDrawColor(renderer,
                               is_active ? 48 : 28,
                               is_active ? 48 : 28,
                               is_active ? 48 : 28,
                               255);
        SDL_Rect tab_rect = {x + 1, 1, TAB_WIDTH - 2, TAB_BAR_HEIGHT - 1};
        SDL_RenderFillRect(renderer, &tab_rect);

        /* Blue top accent on active tab */
        if (is_active) {
            SDL_SetRenderDrawColor(renderer, 80, 160, 255, 255);
            SDL_Rect accent = {x + 1, 1, TAB_WIDTH - 2, 2};
            SDL_RenderFillRect(renderer, &accent);
        }

        /* Title text */
        Uint8 tr = is_active ? 220 : 120;
        Uint8 tg = is_active ? 220 : 120;
        Uint8 tb = is_active ? 220 : 120;

        int text_y = (TAB_BAR_HEIGHT - font->cell_height) / 2;
        int text_x = x + 8;

        char display[20];
        snprintf(display, sizeof(display), "%.14s", tm->tabs[i].title);
        font_draw_string(font, renderer, display,
                         text_x, text_y, tr, tg, tb);

        /* Close "x" button — right side of tab */
        int close_x = x + TAB_WIDTH - font->cell_width - 8;
        font_draw_char(font, renderer, 'x',
                       close_x, text_y, 160, 70, 70);

        /* Vertical separator between tabs */
        SDL_SetRenderDrawColor(renderer, 55, 55, 55, 255);
        SDL_RenderDrawLine(renderer,
                           x + TAB_WIDTH - 1, 3,
                           x + TAB_WIDTH - 1, TAB_BAR_HEIGHT - 3);
    }

    /* ── "+" new-tab button ── */
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

    /* ── Bottom separator line ── */
    SDL_SetRenderDrawColor(renderer, 55, 55, 55, 255);
    SDL_RenderDrawLine(renderer,
                       0, TAB_BAR_HEIGHT - 1,
                       win_width, TAB_BAR_HEIGHT - 1);
}


/* ── tabs_handle_click ──────────────────────────────────────── */

int tabs_handle_click(TabManager *tm, int mouse_x, int mouse_y,
                      int cols, int rows) {
    /* Only handle clicks within the tab bar height */
    if (mouse_y < 0 || mouse_y >= TAB_BAR_HEIGHT) return 0;

    /* "+" button */
    int plus_x = tm->count * TAB_WIDTH;
    if (mouse_x >= plus_x && mouse_x < plus_x + 40) {
        tabs_new(tm, cols, rows);
        return 1;
    }

    /* Which tab? */
    int i = mouse_x / TAB_WIDTH;
    if (i < 0 || i >= tm->count) return 0;

    /* Close button = rightmost 20px of the tab */
    int close_start = (i * TAB_WIDTH) + TAB_WIDTH - 20;
    if (mouse_x >= close_start)
        tabs_close(tm, i);
    else
        tabs_set_active(tm, i);

    return 1;
}


/* ── tabs_destroy ───────────────────────────────────────────── */

void tabs_destroy(TabManager *tm) {
    for (int i = 0; i < tm->count; i++) {
        if (tm->tabs[i].alive && tm->tabs[i].root) {
            pane_destroy(tm->tabs[i].root);
            tm->tabs[i].root  = NULL;
            tm->tabs[i].alive = 0;
        }
    }
    tm->count  = 0;
    tm->active = 0;
    printf("All tabs destroyed.\n");
}