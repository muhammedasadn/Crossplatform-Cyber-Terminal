#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "window.h"
#include "font.h"
#include "pty.h"
#include "ansi.h"

int main(void) {
    Window   win;
    Font     font;
    PTY      pty;
    Terminal *term;

    if (window_init(&win, 800, 500) != 0) return 1;

    if (font_init(&font, win.renderer, "../assets/font.ttf", 16) != 0) {
        window_destroy(&win);
        return 1;
    }

    int cols = win.width  / font.cell_width;
    int rows = win.height / font.cell_height;

    term = terminal_create(cols, rows);
    if (!term) {
        font_destroy(&font);
        window_destroy(&win);
        return 1;
    }

    if (pty_init(&pty, cols, rows) != 0) {
        terminal_destroy(term);
        font_destroy(&font);
        window_destroy(&win);
        return 1;
    }

    char read_buf[4096];
    int  running = 1;

    while (running) {

        /* ── Events ─────────────────────────────────────────── */
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = 0;

            if (event.type == SDL_TEXTINPUT) {
                pty_write(&pty, event.text.text,
                          strlen(event.text.text));
            }

            if (event.type == SDL_KEYDOWN) {
                SDL_Keycode sym = event.key.keysym.sym;
                SDL_Keymod  mod = SDL_GetModState();

                /* Ctrl+key combinations */
                if (mod & KMOD_CTRL) {
                    if (sym >= SDLK_a && sym <= SDLK_z) {
                        /* Ctrl+A = 0x01, Ctrl+B = 0x02, etc. */
                        char ctrl_char = (char)(sym - SDLK_a + 1);
                        pty_write(&pty, &ctrl_char, 1);
                    }
                    if (sym == SDLK_c) {
                        char ctrl_c = 0x03;
                        pty_write(&pty, &ctrl_c, 1);
                    }
                }

                switch (sym) {
                    case SDLK_RETURN:
                        pty_write(&pty, "\r", 1);     break;
                    case SDLK_BACKSPACE:
                        pty_write(&pty, "\x7f", 1);   break;
                    case SDLK_TAB:
                        pty_write(&pty, "\t", 1);     break;
                    case SDLK_ESCAPE:
                        pty_write(&pty, "\x1b", 1);   break;
                    case SDLK_UP:
                        pty_write(&pty, "\x1b[A", 3); break;
                    case SDLK_DOWN:
                        pty_write(&pty, "\x1b[B", 3); break;
                    case SDLK_RIGHT:
                        pty_write(&pty, "\x1b[C", 3); break;
                    case SDLK_LEFT:
                        pty_write(&pty, "\x1b[D", 3); break;
                    case SDLK_HOME:
                        pty_write(&pty, "\x1b[H", 3); break;
                    case SDLK_END:
                        pty_write(&pty, "\x1b[F", 3); break;
                    case SDLK_DELETE:
                        pty_write(&pty, "\x1b[3~", 4);break;
                }
            }

            /* Handle window resize */
            if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    win.width  = event.window.data1;
                    win.height = event.window.data2;
                    int new_cols = win.width  / font.cell_width;
                    int new_rows = win.height / font.cell_height;
                    terminal_resize(term, new_cols, new_rows);
                    pty_resize(&pty, new_cols, new_rows);
                }
            }
        }

        /* ── Read PTY output → feed parser ──────────────────── */
        int n = pty_read(&pty, read_buf, sizeof(read_buf));
        if (n > 0) {
            terminal_process(term, read_buf, n);
        }

        /* ── Render ──────────────────────────────────────────── */
        window_render_begin(&win);

        for (int row = 0; row < term->rows; row++) {
            for (int col = 0; col < term->cols; col++) {
                Cell *cell = &term->cells[row][col];

                int x = col * font.cell_width;
                int y = row * font.cell_height;

                /* Draw background */
                SDL_SetRenderDrawColor(win.renderer,
                    cell->bg.r, cell->bg.g, cell->bg.b, 255);
                SDL_Rect bg_rect = {x, y,
                    font.cell_width, font.cell_height};
                SDL_RenderFillRect(win.renderer, &bg_rect);

                /* Draw character */
                if (cell->ch != ' ') {
                    font_draw_char(&font, win.renderer,
                        cell->ch, x, y,
                        cell->fg.r, cell->fg.g, cell->fg.b);
                }
            }
        }

        /* Draw blinking block cursor */
        Uint32 ticks = SDL_GetTicks();
        if ((ticks / 500) % 2 == 0) {  /* blink every 500ms */
            SDL_SetRenderDrawColor(win.renderer, 220, 220, 220, 200);
            SDL_Rect cur = {
                term->cursor_col * font.cell_width,
                term->cursor_row * font.cell_height,
                font.cell_width,
                font.cell_height
            };
            SDL_RenderFillRect(win.renderer, &cur);
        }

        window_render_end(&win);
    }

    pty_destroy(&pty);
    terminal_destroy(term);
    font_destroy(&font);
    window_destroy(&win);
    return 0;
}