/*
 * main.c — cterm entry point.
 *
 * Wires all modules together:
 *   window   → SDL2 window + GPU renderer
 *   font     → FreeType glyph cache
 *   tabs     → tab bar + TabManager
 *   pane     → split pane tree per tab
 *   ansi     → ANSI/VT100 parser + cell grid
 *   pty      → shell process (one per leaf pane)
 *
 * Main loop each frame:
 *   1. Poll SDL2 events (keyboard, mouse, resize, quit)
 *   2. Read PTY output for every pane in every tab
 *   3. Compute pane layout (pixel rects)
 *   4. Render tab bar
 *   5. Render all leaf panes of the active tab
 *   6. Draw pane dividers + focus border
 *   7. Draw cursor + scroll indicator
 */

#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "window.h"
#include "font.h"
#include "tabs.h"
#include "pane.h"
#include "ansi.h"


/* ── render_pane_tree ───────────────────────────────────────── */

/*
 * Recursively renders every leaf pane in the tree.
 *
 * For PANE_LEAF:
 *   - Recomputes cols/rows from the pane's pixel rect.
 *   - Calls terminal_resize + pty_resize if dimensions changed.
 *   - Draws background rects and character glyphs for every cell.
 *   - Draws the blinking cursor if this pane is focused.
 *
 * For PANE_SPLIT:
 *   - Recurses into first and second children.
 *
 * Called once per frame after pane_layout() has computed rects.
 */
static void render_pane_tree(Pane *p, SDL_Renderer *renderer,
                             Font *font) {
    if (!p) return;

    if (p->type != PANE_LEAF) {
        /* Split node — recurse into both children */
        render_pane_tree(p->first,  renderer, font);
        render_pane_tree(p->second, renderer, font);
        return;
    }

    /* ── Leaf pane ── */
    Terminal *term  = p->term;
    SDL_Rect  r     = p->rect;

    /* Skip degenerate rects */
    if (r.w < font->cell_width || r.h < font->cell_height) return;

    /* Recompute grid dimensions from pixel rect */
    int pcols = r.w / font->cell_width;
    int prows = r.h / font->cell_height;
    if (pcols < 1) pcols = 1;
    if (prows < 1) prows = 1;

    /* Resize terminal + PTY if the pane size changed */
    if (pcols != term->cols || prows != term->rows) {
        terminal_resize(term, pcols, prows);
        pty_resize(&p->pty, pcols, prows);
    }

    /* ── Draw each cell ── */
    for (int row = 0; row < term->rows; row++) {
        Cell *display_row = terminal_get_display_row(term, row);

        for (int col = 0; col < term->cols; col++) {
            int x = r.x + col * font->cell_width;
            int y = r.y + row * font->cell_height;

            /* Skip cells that would draw outside the pane rect */
            if (x + font->cell_width  > r.x + r.w) continue;
            if (y + font->cell_height > r.y + r.h) continue;

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

            /* Cell background */
            SDL_SetRenderDrawColor(renderer, bg_r, bg_g, bg_b, 255);
            SDL_Rect bg_rect = {x, y, font->cell_width, font->cell_height};
            SDL_RenderFillRect(renderer, &bg_rect);

            /* Character glyph */
            if (ch != ' ' && ch != '\0') {
                font_draw_char(font, renderer, ch, x, y,
                               fg_r, fg_g, fg_b);
            }
        }
    }

    /* ── Blinking cursor (focused pane, live view only) ── */
    if (p->focused && term->scroll_offset == 0) {
        Uint32 ticks = SDL_GetTicks();
        if ((ticks / 500) % 2 == 0) {
            SDL_SetRenderDrawColor(renderer, 220, 220, 220, 200);
            SDL_Rect cur = {
                r.x + term->cursor_col * font->cell_width,
                r.y + term->cursor_row * font->cell_height,
                font->cell_width,
                font->cell_height
            };
            SDL_RenderFillRect(renderer, &cur);
        }
    }

    /* ── Scroll indicator (focused pane, scrolled view) ── */
    if (p->focused && term->scroll_offset > 0) {
        /* Blue line at top of pane */
        SDL_SetRenderDrawColor(renderer, 80, 140, 255, 220);
        SDL_Rect top_line = {r.x, r.y, r.w, 2};
        SDL_RenderFillRect(renderer, &top_line);

        /* Proportional scroll bar on right edge */
        if (term->sb_count > 0) {
            float ratio = (float)term->scroll_offset
                          / (float)term->sb_count;
            int bar_h   = r.h / 8;
            int bar_y   = r.y + (int)((r.h - bar_h) * (1.0f - ratio));
            SDL_SetRenderDrawColor(renderer, 80, 140, 255, 160);
            SDL_Rect bar = {r.x + r.w - 4, bar_y, 4, bar_h};
            SDL_RenderFillRect(renderer, &bar);
        }
    }
}


/* ════════════════════════════════════════════════════════════
 * main
 * ════════════════════════════════════════════════════════════ */

int main(void) {
    Window     win;
    Font       font;
    TabManager tm;

    /* ── Window ──────────────────────────────────────────────── */
    if (window_init(&win, 900, 550) != 0) {
        fprintf(stderr, "Failed to create window.\n");
        return 1;
    }

    /* ── Font ────────────────────────────────────────────────── */
    if (font_init(&font, win.renderer, "../assets/font.ttf", 16) != 0) {
        fprintf(stderr, "Failed to load font.\n");
        window_destroy(&win);
        return 1;
    }

    /* ── Initial grid dimensions ─────────────────────────────── */
    int cols = win.width  / font.cell_width;
    int rows = (win.height - TAB_BAR_HEIGHT) / font.cell_height;

    /* ── Tab manager — opens first tab + bash ────────────────── */
    if (tabs_init(&tm, cols, rows) != 0) {
        fprintf(stderr, "Failed to init tabs.\n");
        font_destroy(&font);
        window_destroy(&win);
        return 1;
    }

    char read_buf[4096];
    int  running = 1;

    /* ════════════════════════════════════════════════════════════
     * MAIN LOOP
     * ════════════════════════════════════════════════════════════ */
    while (running) {

        /* ── Refresh active tab pointer each frame ───────────── */
        Tab  *tab = tabs_get_active(&tm);

        /* ── 1. EVENT HANDLING ───────────────────────────────── */
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
                cols = win.width  / font.cell_width;
                rows = (win.height - TAB_BAR_HEIGHT)
                       / font.cell_height;
                /*
                 * Resize happens in render_pane_tree automatically
                 * when leaf pane rects change. No explicit call needed.
                 */
                continue;
            }

            /* ── Mouse button ── */
            if (event.type == SDL_MOUSEBUTTONDOWN &&
                event.button.button == SDL_BUTTON_LEFT) {

                int mx = event.button.x;
                int my = event.button.y;

                /* Tab bar click? */
                if (my < TAB_BAR_HEIGHT) {
                    if (tabs_handle_click(&tm, mx, my, cols, rows)) {
                        tab = tabs_get_active(&tm);
                    }
                } else {
                    /*
                     * Click inside terminal area — focus the pane
                     * that was clicked. pane_find_at searches the
                     * tree by pixel position.
                     */
                    Pane *clicked = pane_find_at(tab->root, mx, my);
                    if (clicked) {
                        pane_set_focus(tab->root, clicked);
                    }
                }
                continue;
            }

            /* ── Mouse wheel — scrollback on focused pane ── */
            if (event.type == SDL_MOUSEWHEEL) {
                Pane *fp = pane_get_focused(tab->root);
                if (fp && fp->term) {
                    if (event.wheel.y > 0) {
                        fp->term->scroll_offset += 3;
                        if (fp->term->scroll_offset > fp->term->sb_count)
                            fp->term->scroll_offset = fp->term->sb_count;
                    } else if (event.wheel.y < 0) {
                        fp->term->scroll_offset -= 3;
                        if (fp->term->scroll_offset < 0)
                            fp->term->scroll_offset = 0;
                    }
                }
                continue;
            }

            /* ── Text input — printable characters ── */
            /*
             * SDL_TEXTINPUT fires ONLY for printable characters.
             * It never fires when Ctrl or Alt is held, so it cannot
             * interfere with Ctrl+T, Ctrl+W, etc.
             */
            if (event.type == SDL_TEXTINPUT) {
                Pane *fp = pane_get_focused(tab->root);
                if (fp) {
                    fp->term->scroll_offset = 0;
                    pty_write(&fp->pty, event.text.text,
                              strlen(event.text.text));
                }
                continue;
            }

            /* ── Key down — Ctrl combos + special keys ── */
            if (event.type == SDL_KEYDOWN) {
                SDL_Keycode sym = event.key.keysym.sym;
                SDL_Keymod  mod = SDL_GetModState();

                int ctrl  = (mod & KMOD_CTRL)  != 0;
                int shift = (mod & KMOD_SHIFT) != 0;

                /* ══ TAB SHORTCUTS ══════════════════════════════ */
                /* Checked BEFORE generic Ctrl+letter handler.     */

                /* Ctrl+T — new tab */
                if (ctrl && !shift && sym == SDLK_t) {
                    tabs_new(&tm, cols, rows);
                    tab = tabs_get_active(&tm);
                    continue;
                }

                /* Ctrl+W — close current tab */
                if (ctrl && !shift && sym == SDLK_w) {
                    tabs_close(&tm, tm.active);
                    tab = tabs_get_active(&tm);
                    continue;
                }

                /* Ctrl+Tab — next tab */
                if (ctrl && !shift && sym == SDLK_TAB) {
                    tabs_next(&tm);
                    tab = tabs_get_active(&tm);
                    continue;
                }

                /* Ctrl+Shift+Tab — previous tab */
                if (ctrl && shift && sym == SDLK_TAB) {
                    tabs_prev(&tm);
                    tab = tabs_get_active(&tm);
                    continue;
                }

                /* Ctrl+1 through Ctrl+9 — jump to tab N */
                if (ctrl && !shift &&
                    sym >= SDLK_1 && sym <= SDLK_9) {
                    tabs_set_active(&tm, sym - SDLK_1);
                    tab = tabs_get_active(&tm);
                    continue;
                }

                /* ══ PANE SHORTCUTS ═════════════════════════════ */

                /* Ctrl+Shift+Right — split focused pane horizontally */
                if (ctrl && shift && sym == SDLK_RIGHT) {
                    Pane *fp = pane_get_focused(tab->root);
                    if (fp) {
                        /*
                         * pane_split replaces the leaf with a split
                         * node. If fp is the root, we replace the root.
                         * For deeper panes we need parent tracking —
                         * for now we replace root in all cases via
                         * the return value.
                         */
                        Pane *new_node = pane_split(fp,
                                             PANE_SPLIT_H,
                                             cols, rows);
                        if (new_node != fp) {
                            /*
                             * fp was the root leaf. Replace root.
                             * For non-root splits the tree is updated
                             * inside pane_split via pointer rewriting.
                             */
                            tab->root = new_node;
                        }
                    }
                    continue;
                }

                /* Ctrl+Shift+Down — split focused pane vertically */
                if (ctrl && shift && sym == SDLK_DOWN) {
                    Pane *fp = pane_get_focused(tab->root);
                    if (fp) {
                        Pane *new_node = pane_split(fp,
                                             PANE_SPLIT_V,
                                             cols, rows);
                        if (new_node != fp) {
                            tab->root = new_node;
                        }
                    }
                    continue;
                }

                /* Ctrl+Shift+W — close focused pane */
                if (ctrl && shift && sym == SDLK_w) {
                    tab->root = pane_close_focused(tab->root);
                    if (!tab->root) {
                        /* All panes in this tab are gone */
                        tabs_close(&tm, tm.active);
                        tab = tabs_get_active(&tm);
                    }
                    continue;
                }

                /* Ctrl+Shift+F — cycle focus between panes */
                if (ctrl && shift && sym == SDLK_f) {
                    pane_focus_next(tab->root);
                    continue;
                }

                /* ══ GENERIC Ctrl+letter ════════════════════════ */
                /* Only reached if NOT a tab/pane shortcut above.  */
                if (ctrl && sym >= SDLK_a && sym <= SDLK_z) {
                    Pane *fp = pane_get_focused(tab->root);
                    if (fp) {
                        char ctrl_char = (char)(sym - SDLK_a + 1);
                        pty_write(&fp->pty, &ctrl_char, 1);
                    }
                    continue;
                }

                /* ══ TERMINAL KEYS ══════════════════════════════ */
                {
                    Pane *fp = pane_get_focused(tab->root);
                    if (!fp) continue;

                    PTY      *pty  = &fp->pty;
                    Terminal *term_fp = fp->term;

                    switch (sym) {
                        case SDLK_RETURN:
                            term_fp->scroll_offset = 0;
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
                            term_fp->scroll_offset += rows / 2;
                            if (term_fp->scroll_offset > term_fp->sb_count)
                                term_fp->scroll_offset = term_fp->sb_count;
                            break;
                        case SDLK_PAGEDOWN:
                            term_fp->scroll_offset -= rows / 2;
                            if (term_fp->scroll_offset < 0)
                                term_fp->scroll_offset = 0;
                            break;
                        default:
                            break;
                    }
                }
                continue;

            } /* end SDL_KEYDOWN */

        } /* end SDL_PollEvent */


        /* ── 2. READ PTY OUTPUT ──────────────────────────────── */
        /*
         * Read from every pane in every tab every frame.
         * Background tabs keep running — their output is parsed
         * silently into their grids. Switching to them shows
         * up-to-date content immediately.
         */
        for (int i = 0; i < tm.count; i++) {
            pane_read_all(tm.tabs[i].root, read_buf, sizeof(read_buf));
        }


        /* ── 3. RENDER ───────────────────────────────────────── */
        window_render_begin(&win);

        /* ── 3a. Tab bar ── */
        tabs_draw_bar(&tm, win.renderer, &font, win.width);

        /* ── 3b. Compute pane layout ── */
        /*
         * pane_layout() recursively fills every pane's rect field
         * based on the available terminal area (below the tab bar).
         * Must be called before rendering so rects are current.
         */
        tab = tabs_get_active(&tm);
        SDL_Rect terminal_area = {
            0,
            TAB_BAR_HEIGHT,
            win.width,
            win.height - TAB_BAR_HEIGHT
        };
        pane_layout(tab->root, terminal_area);

        /* ── 3c. Render all leaf panes ── */
        render_pane_tree(tab->root, win.renderer, &font);

        /* ── 3d. Draw dividers + focus border ── */
        pane_draw_dividers(tab->root, win.renderer);

        window_render_end(&win);

    } /* end main loop */


    /* ── Cleanup ─────────────────────────────────────────────── */
    tabs_destroy(&tm);
    font_destroy(&font);
    window_destroy(&win);
    return 0;
}