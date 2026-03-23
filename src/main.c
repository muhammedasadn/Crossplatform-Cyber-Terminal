/*
 * main.c — cterm with working tab support.
 *
 * Key fix: SDL_TEXTINPUT fires for printable characters.
 * SDL_KEYDOWN fires for ALL keys including Ctrl combos.
 * When Ctrl is held, SDL_TEXTINPUT does NOT fire — only
 * SDL_KEYDOWN fires. So all Ctrl+key logic lives in
 * SDL_KEYDOWN only, and we check tab shortcuts FIRST
 * before the generic Ctrl+letter handler.
 */

#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "window.h"
#include "font.h"
#include "tabs.h"
#include "ansi.h"

int main(void) {
    Window     win;
    Font       font;
    TabManager tm;

    if (window_init(&win, 900, 550) != 0) {
        fprintf(stderr, "Failed to create window.\n");
        return 1;
    }

    if (font_init(&font, win.renderer, "../assets/font.ttf", 16) != 0) {
        fprintf(stderr, "Failed to load font.\n");
        window_destroy(&win);
        return 1;
    }

    int cols = win.width  / font.cell_width;
    int rows = (win.height - TAB_BAR_HEIGHT) / font.cell_height;

    if (tabs_init(&tm, cols, rows) != 0) {
        fprintf(stderr, "Failed to init tabs.\n");
        font_destroy(&font);
        window_destroy(&win);
        return 1;
    }

    char read_buf[4096];
    int  running = 1;

    while (running) {

        /* Always get fresh pointers at top of loop */
        Tab      *tab  = tabs_get_active(&tm);
        Terminal *term = tab->term;
        PTY      *pty  = &tab->pty;

        /* ── Event loop ──────────────────────────────────────── */
        SDL_Event event;
        while (SDL_PollEvent(&event)) {

            /* ── Quit ── */
            if (event.type == SDL_QUIT) {
                running = 0;
                continue;
            }

            /* ── Window resize ── */
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_RESIZED) {
                win.width  = event.window.data1;
                win.height = event.window.data2;
                int nc = win.width  / font.cell_width;
                int nr = (win.height - TAB_BAR_HEIGHT)
                         / font.cell_height;
                for (int i = 0; i < tm.count; i++) {
                    terminal_resize(tm.tabs[i].term, nc, nr);
                    pty_resize(&tm.tabs[i].pty, nc, nr);
                }
                cols = nc;
                rows = nr;
                continue;
            }

            /* ── Mouse button — tab bar clicks ── */
            if (event.type == SDL_MOUSEBUTTONDOWN &&
                event.button.button == SDL_BUTTON_LEFT) {
                if (tabs_handle_click(&tm,
                                      event.button.x,
                                      event.button.y,
                                      cols, rows)) {
                    /* Click was inside tab bar — refresh pointers */
                    tab  = tabs_get_active(&tm);
                    term = tab->term;
                    pty  = &tab->pty;
                }
                continue;
            }

            /* ── Mouse wheel — scrollback ── */
            if (event.type == SDL_MOUSEWHEEL) {
                if (event.wheel.y > 0) {
                    term->scroll_offset += 3;
                    if (term->scroll_offset > term->sb_count)
                        term->scroll_offset = term->sb_count;
                } else if (event.wheel.y < 0) {
                    term->scroll_offset -= 3;
                    if (term->scroll_offset < 0)
                        term->scroll_offset = 0;
                }
                continue;
            }

            /* ── Text input — printable characters ── */
            /*
             * SDL_TEXTINPUT fires ONLY for printable chars.
             * It does NOT fire when Ctrl/Alt is held.
             * So this block never interferes with Ctrl+T etc.
             */
            if (event.type == SDL_TEXTINPUT) {
                term->scroll_offset = 0;
                pty_write(pty, event.text.text,
                          strlen(event.text.text));
                continue;
            }

            /* ── Key down — special keys and Ctrl combos ── */
            if (event.type == SDL_KEYDOWN) {
                SDL_Keycode sym = event.key.keysym.sym;
                SDL_Keymod  mod = SDL_GetModState();

                int ctrl  = (mod & KMOD_CTRL)  != 0;
                int shift = (mod & KMOD_SHIFT) != 0;

                /*
                 * TAB SHORTCUTS — must be checked before the
                 * generic Ctrl+letter handler below.
                 * Each one refreshes tab/term/pty after switching.
                 */
                if (ctrl && sym == SDLK_t) {
                    /* Ctrl+T — open new tab */
                    printf("DEBUG: Ctrl+T pressed, opening new tab\n");
                    fflush(stdout);
                    tabs_new(&tm, cols, rows);
                    tab  = tabs_get_active(&tm);
                    term = tab->term;
                    pty  = &tab->pty;
                    continue;
                }

                if (ctrl && sym == SDLK_w) {
                    /* Ctrl+W — close current tab */
                    tabs_close(&tm, tm.active);
                    tab  = tabs_get_active(&tm);
                    term = tab->term;
                    pty  = &tab->pty;
                    continue;
                }

                if (ctrl && sym == SDLK_TAB) {
                    /* Ctrl+Tab / Ctrl+Shift+Tab */
                    if (shift) tabs_prev(&tm);
                    else       tabs_next(&tm);
                    tab  = tabs_get_active(&tm);
                    term = tab->term;
                    pty  = &tab->pty;
                    continue;
                }

                if (ctrl && sym >= SDLK_1 && sym <= SDLK_9) {
                    /* Ctrl+1 through Ctrl+9 — jump to tab */
                    tabs_set_active(&tm, sym - SDLK_1);
                    tab  = tabs_get_active(&tm);
                    term = tab->term;
                    pty  = &tab->pty;
                    continue;
                }

                /*
                 * GENERIC Ctrl+letter — send control byte to shell.
                 * Reached only if NOT a tab shortcut above.
                 * Ctrl+A=0x01, Ctrl+B=0x02, ..., Ctrl+Z=0x1A
                 */
                if (ctrl && sym >= SDLK_a && sym <= SDLK_z) {
                    char ctrl_char = (char)(sym - SDLK_a + 1);
                    pty_write(pty, &ctrl_char, 1);
                    continue;
                }

                /*
                 * TERMINAL KEYS — arrow keys, Enter, Backspace etc.
                 * These only fire when Ctrl is NOT held (printable
                 * chars are handled by SDL_TEXTINPUT above).
                 */
                switch (sym) {
                    case SDLK_RETURN:
                        term->scroll_offset = 0;
                        pty_write(pty, "\r", 1);
                        break;
                    case SDLK_BACKSPACE:
                        pty_write(pty, "\x7f", 1);
                        break;
                    case SDLK_TAB:
                        pty_write(pty, "\t", 1);
                        break;
                    case SDLK_ESCAPE:
                        pty_write(pty, "\x1b", 1);
                        break;
                    case SDLK_UP:
                        pty_write(pty, "\x1b[A", 3);
                        break;
                    case SDLK_DOWN:
                        pty_write(pty, "\x1b[B", 3);
                        break;
                    case SDLK_RIGHT:
                        pty_write(pty, "\x1b[C", 3);
                        break;
                    case SDLK_LEFT:
                        pty_write(pty, "\x1b[D", 3);
                        break;
                    case SDLK_HOME:
                        pty_write(pty, "\x1b[H", 3);
                        break;
                    case SDLK_END:
                        pty_write(pty, "\x1b[F", 3);
                        break;
                    case SDLK_DELETE:
                        pty_write(pty, "\x1b[3~", 4);
                        break;
                    case SDLK_PAGEUP:
                        term->scroll_offset += rows / 2;
                        if (term->scroll_offset > term->sb_count)
                            term->scroll_offset = term->sb_count;
                        break;
                    case SDLK_PAGEDOWN:
                        term->scroll_offset -= rows / 2;
                        if (term->scroll_offset < 0)
                            term->scroll_offset = 0;
                        break;
                    default:
                        break;
                }
                continue;
            }

        } /* end SDL_PollEvent */


        /* ── Read PTY output for ALL tabs ────────────────────── */
        for (int i = 0; i < tm.count; i++) {
            int n = pty_read(&tm.tabs[i].pty,
                             read_buf, sizeof(read_buf));
            if (n > 0) {
                terminal_process(tm.tabs[i].term, read_buf, n);
            }
        }


        /* ── Render ──────────────────────────────────────────── */
        window_render_begin(&win);

        /* Tab bar */
        tabs_draw_bar(&tm, win.renderer, &font, win.width);

        /* Active terminal grid */
        tab  = tabs_get_active(&tm);
        term = tab->term;

        for (int row = 0; row < term->rows; row++) {
            Cell *display_row = terminal_get_display_row(term, row);

            for (int col = 0; col < term->cols; col++) {
                int x = col * font.cell_width;
                int y = row * font.cell_height + TAB_BAR_HEIGHT;

                Uint8 bg_r = 0,   bg_g = 0,   bg_b = 0;
                Uint8 fg_r = 200, fg_g = 200, fg_b = 200;
                char  ch   = ' ';

                if (display_row) {
                    ch   = display_row[col].ch;
                    fg_r = display_row[col].fg.r;
                    fg_g = display_row[col].fg.g;
                    fg_b = display_row[col].fg.b;
                    bg_r = display_row[col].bg.r;
                    bg_g = display_row[col].bg.g;
                    bg_b = display_row[col].bg.b;
                }

                SDL_SetRenderDrawColor(win.renderer,
                                       bg_r, bg_g, bg_b, 255);
                SDL_Rect bg_rect = {x, y,
                                    font.cell_width,
                                    font.cell_height};
                SDL_RenderFillRect(win.renderer, &bg_rect);

                if (ch != ' ' && ch != '\0') {
                    font_draw_char(&font, win.renderer,
                                   ch, x, y,
                                   fg_r, fg_g, fg_b);
                }
            }
        }

        /* Blinking cursor — only in live view */
        if (term->scroll_offset == 0) {
            Uint32 ticks = SDL_GetTicks();
            if ((ticks / 500) % 2 == 0) {
                SDL_SetRenderDrawColor(win.renderer,
                                       220, 220, 220, 200);
                SDL_Rect cur = {
                    term->cursor_col * font.cell_width,
                    term->cursor_row * font.cell_height
                        + TAB_BAR_HEIGHT,
                    font.cell_width,
                    font.cell_height
                };
                SDL_RenderFillRect(win.renderer, &cur);
            }
        }

        /* Scroll indicator */
        if (term->scroll_offset > 0) {
            SDL_SetRenderDrawColor(win.renderer, 80, 140, 255, 220);
            SDL_Rect top_line = {0, TAB_BAR_HEIGHT, win.width, 2};
            SDL_RenderFillRect(win.renderer, &top_line);

            if (term->sb_count > 0) {
                float ratio = (float)term->scroll_offset
                              / (float)term->sb_count;
                int bar_h = win.height / 8;
                int bar_y = TAB_BAR_HEIGHT
                            + (int)((win.height - TAB_BAR_HEIGHT
                                     - bar_h) * (1.0f - ratio));
                SDL_SetRenderDrawColor(win.renderer,
                                       80, 140, 255, 160);
                SDL_Rect bar = {win.width - 4, bar_y, 4, bar_h};
                SDL_RenderFillRect(win.renderer, &bar);
            }
        }

        window_render_end(&win);

    } /* end main loop */

    tabs_destroy(&tm);
    font_destroy(&font);
    window_destroy(&win);
    return 0;
}