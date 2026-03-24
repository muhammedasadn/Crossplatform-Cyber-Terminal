/*
 * ansi.c — ANSI/VT100 terminal emulator implementation.
 *
 * All known bugs fixed in this version:
 *
 *  Fix 1 — Path duplication:
 *    \n now resets cursor_col=0 (matches xterm behavior).
 *    Previously only \r reset it — programs sending bare \n
 *    would print on wrong columns causing duplicated paths.
 *
 *  Fix 2 — Readline history garbled (Up/Down arrows):
 *    clear_cell() now resets fg, bg, bold — not just ch.
 *    Without resetting bg, "erased" cells kept colored
 *    backgrounds so old command text was visible through
 *    the erase. EL(\x1b[K) uses clear_cell() everywhere.
 *
 *  Fix 3 — CSI parser robustness:
 *    Added '>' and ' ' to intermediate byte set in STATE_CSI.
 *    bash sends \x1b[>0c (device attrs) at startup — without
 *    '>' the parser aborted mid-sequence corrupting state.
 *
 *  Fix 4 — Delete key in readline (DCH \x1b[P):
 *    Implemented case 'P': shifts characters left after cursor.
 *    Without this, pressing Delete left ghost characters.
 *
 *  Fix 5 — Typing mid-line in readline (ICH \x1b[@):
 *    Implemented case '@': shifts characters right at cursor.
 *    Without this, typing in the middle overwrote instead of
 *    inserting.
 *
 *  Fix 6 — NUL/DEL bytes ignored:
 *    0x00 and 0x7f now explicitly ignored. Previously they
 *    fell through to printable range and drew garbage.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "ansi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ── Default colors ─────────────────────────────────────────── */

static const Color DEFAULT_FG = {200, 200, 200};
static const Color DEFAULT_BG = {  0,   0,   0};


/* ── terminal_create ────────────────────────────────────────── */

Terminal *terminal_create(int cols, int rows) {
    Terminal *t = calloc(1, sizeof(Terminal));
    if (!t) return NULL;

    t->cols          = cols;
    t->rows          = rows;
    t->current_fg    = DEFAULT_FG;
    t->current_bg    = DEFAULT_BG;
    t->state         = STATE_NORMAL;
    t->scroll_offset = 0;
    t->sb_head       = 0;
    t->sb_count      = 0;

    t->cells = calloc(rows, sizeof(Cell *));
    if (!t->cells) { free(t); return NULL; }

    for (int r = 0; r < rows; r++) {
        t->cells[r] = calloc(cols, sizeof(Cell));
        if (!t->cells[r]) { terminal_destroy(t); return NULL; }
        for (int c = 0; c < cols; c++) {
            t->cells[r][c].ch    = ' ';
            t->cells[r][c].fg    = DEFAULT_FG;
            t->cells[r][c].bg    = DEFAULT_BG;
            t->cells[r][c].bold  = 0;
            t->cells[r][c].dirty = 1;
        }
    }
    return t;
}


/* ── terminal_destroy ───────────────────────────────────────── */

void terminal_destroy(Terminal *t) {
    if (!t) return;
    if (t->cells) {
        for (int r = 0; r < t->rows; r++)
            free(t->cells[r]);
        free(t->cells);
    }
    for (int i = 0; i < SCROLLBACK_MAX; i++)
        if (t->scrollback[i].cells)
            free(t->scrollback[i].cells);
    free(t);
}


/* ── Internal helpers ───────────────────────────────────────── */

/*
 * clear_cell — set one cell to blank with DEFAULT colors.
 *
 * WHY we reset bg here (not just ch):
 * If a cell previously had a colored background (e.g. green
 * from a bash prompt color) and we only set ch=' ', the cell
 * is logically blank but the renderer still draws its bg color.
 * This makes old text appear to "shine through" the erase.
 * Resetting bg=DEFAULT_BG makes the cell truly invisible.
 */
static void clear_cell(Terminal *t, int row, int col) {
    if (row < 0 || row >= t->rows) return;
    if (col < 0 || col >= t->cols) return;
    t->cells[row][col].ch    = ' ';
    t->cells[row][col].fg    = DEFAULT_FG;
    t->cells[row][col].bg    = DEFAULT_BG;
    t->cells[row][col].bold  = 0;
    t->cells[row][col].dirty = 1;
}

static void mark_all_dirty(Terminal *t) {
    for (int r = 0; r < t->rows; r++)
        for (int c = 0; c < t->cols; c++)
            t->cells[r][c].dirty = 1;
}

static void scrollback_push(Terminal *t, Cell *row) {
    ScrollbackLine *slot = &t->scrollback[t->sb_head];
    if (slot->cells) { free(slot->cells); slot->cells = NULL; }

    slot->cells = malloc(t->cols * sizeof(Cell));
    if (!slot->cells) return;
    memcpy(slot->cells, row, t->cols * sizeof(Cell));
    slot->cols = t->cols;

    t->sb_head = (t->sb_head + 1) % SCROLLBACK_MAX;
    if (t->sb_count < SCROLLBACK_MAX) t->sb_count++;
}

static void scroll_up(Terminal *t) {
    scrollback_push(t, t->cells[0]);

    Cell *top = t->cells[0];
    memmove(&t->cells[0], &t->cells[1],
            (t->rows - 1) * sizeof(Cell *));
    t->cells[t->rows - 1] = top;

    for (int c = 0; c < t->cols; c++)
        clear_cell(t, t->rows - 1, c);

    if (t->scroll_offset > 0) {
        t->scroll_offset++;
        if (t->scroll_offset > t->sb_count)
            t->scroll_offset = t->sb_count;
    }
}

static void put_char(Terminal *t, char ch) {
    if (t->cursor_col >= t->cols) {
        t->cursor_col = 0;
        t->cursor_row++;
    }
    if (t->cursor_row >= t->rows) {
        scroll_up(t);
        t->cursor_row = t->rows - 1;
    }
    Cell *cell  = &t->cells[t->cursor_row][t->cursor_col];
    cell->ch    = ch;
    cell->fg    = t->current_fg;
    cell->bg    = t->current_bg;
    cell->bold  = t->bold;
    cell->dirty = 1;
    t->cursor_col++;
}


/* ── CSI parameter parsing ──────────────────────────────────── */

static void parse_params(const char *raw, int *out, int *count) {
    *count = 0;
    const char *p = raw;
    while (*p && *count < MAX_PARAMS) {
        char *end;
        out[(*count)++] = (int)strtol(p, &end, 10);
        if (*end == ';') end++;
        if (end == p) break;
        p = end;
    }
    if (*count == 0) { out[0] = 0; *count = 1; }
}


/* ── SGR — Select Graphic Rendition ────────────────────────── */

static void apply_sgr(Terminal *t, int *params, int count) {
    for (int i = 0; i < count; i++) {
        int p = params[i];
        if (p == 0) {
            t->current_fg = DEFAULT_FG;
            t->current_bg = DEFAULT_BG;
            t->bold       = 0;
        } else if (p == 1) {
            t->bold = 1;
        } else if (p == 2 || p == 22) {
            t->bold = 0;
        } else if (p >= 30 && p <= 37) {
            t->current_fg = ANSI_COLORS[(p-30) + (t->bold ? 8 : 0)];
        } else if (p == 39) {
            t->current_fg = DEFAULT_FG;
        } else if (p >= 40 && p <= 47) {
            t->current_bg = ANSI_COLORS[p - 40];
        } else if (p == 49) {
            t->current_bg = DEFAULT_BG;
        } else if (p >= 90 && p <= 97) {
            t->current_fg = ANSI_COLORS[(p - 90) + 8];
        } else if (p >= 100 && p <= 107) {
            t->current_bg = ANSI_COLORS[(p - 100) + 8];
        } else if (p == 38) {
            if (i+2 < count && params[i+1] == 5) {
                int n = params[i+2];
                if (n >= 0 && n < 16) t->current_fg = ANSI_COLORS[n];
                i += 2;
            } else if (i+4 < count && params[i+1] == 2) {
                t->current_fg.r = (uint8_t)params[i+2];
                t->current_fg.g = (uint8_t)params[i+3];
                t->current_fg.b = (uint8_t)params[i+4];
                i += 4;
            }
        } else if (p == 48) {
            if (i+2 < count && params[i+1] == 5) {
                int n = params[i+2];
                if (n >= 0 && n < 16) t->current_bg = ANSI_COLORS[n];
                i += 2;
            } else if (i+4 < count && params[i+1] == 2) {
                t->current_bg.r = (uint8_t)params[i+2];
                t->current_bg.g = (uint8_t)params[i+3];
                t->current_bg.b = (uint8_t)params[i+4];
                i += 4;
            }
        }
        /* All other SGR codes silently ignored */
    }
}


/* ── CSI command dispatch ───────────────────────────────────── */

static void apply_csi(Terminal *t, char final) {
    int params[MAX_PARAMS];
    int count = 0;
    parse_params(t->params, params, &count);

    int p0 = params[0];
    int p1 = (count > 1) ? params[1] : 1;
    if (p1 < 1) p1 = 1;

    switch (final) {

        /* ── SGR: colors and text attributes ── */
        case 'm':
            apply_sgr(t, params, count);
            break;

        /* ── Cursor movement ── */
        case 'A': {
            /* CUU — cursor up N rows */
            int n = (p0 < 1) ? 1 : p0;
            t->cursor_row -= n;
            if (t->cursor_row < 0) t->cursor_row = 0;
            break;
        }
        case 'B': {
            /* CUD — cursor down N rows */
            int n = (p0 < 1) ? 1 : p0;
            t->cursor_row += n;
            if (t->cursor_row >= t->rows) t->cursor_row = t->rows-1;
            break;
        }
        case 'C': {
            /* CUF — cursor right N cols */
            int n = (p0 < 1) ? 1 : p0;
            t->cursor_col += n;
            if (t->cursor_col >= t->cols) t->cursor_col = t->cols-1;
            break;
        }
        case 'D': {
            /* CUB — cursor left N cols */
            int n = (p0 < 1) ? 1 : p0;
            t->cursor_col -= n;
            if (t->cursor_col < 0) t->cursor_col = 0;
            break;
        }
        case 'E': {
            /* CNL — cursor next line N times */
            int n = (p0 < 1) ? 1 : p0;
            t->cursor_row += n;
            t->cursor_col  = 0;
            if (t->cursor_row >= t->rows) t->cursor_row = t->rows-1;
            break;
        }
        case 'F': {
            /* CPL — cursor previous line N times */
            int n = (p0 < 1) ? 1 : p0;
            t->cursor_row -= n;
            t->cursor_col  = 0;
            if (t->cursor_row < 0) t->cursor_row = 0;
            break;
        }
        case 'G': {
            /*
             * CHA — cursor horizontal absolute (1-based column).
             *
             * readline sends \x1b[G or \x1b[1G to jump to col 0
             * before erasing and redrawing the command line.
             * Both p0=0 and p0=1 must map to cursor_col=0.
             */
            int col = (p0 < 1) ? 1 : p0;
            t->cursor_col = col - 1;
            if (t->cursor_col < 0)        t->cursor_col = 0;
            if (t->cursor_col >= t->cols) t->cursor_col = t->cols-1;
            break;
        }
        case 'H':
        case 'f': {
            /* CUP — cursor position row;col (both 1-based) */
            int row = (p0 < 1) ? 1 : p0;
            int col = (p1 < 1) ? 1 : p1;
            t->cursor_row = row - 1;
            t->cursor_col = col - 1;
            if (t->cursor_row < 0)        t->cursor_row = 0;
            if (t->cursor_row >= t->rows) t->cursor_row = t->rows-1;
            if (t->cursor_col < 0)        t->cursor_col = 0;
            if (t->cursor_col >= t->cols) t->cursor_col = t->cols-1;
            break;
        }

        /* ── Erase in display ── */
        case 'J': {
            if (p0 == 0) {
                /* Erase from cursor to end of screen */
                for (int c = t->cursor_col; c < t->cols; c++)
                    clear_cell(t, t->cursor_row, c);
                for (int r = t->cursor_row+1; r < t->rows; r++)
                    for (int c = 0; c < t->cols; c++)
                        clear_cell(t, r, c);
            } else if (p0 == 1) {
                /* Erase from start of screen to cursor */
                for (int r = 0; r < t->cursor_row; r++)
                    for (int c = 0; c < t->cols; c++)
                        clear_cell(t, r, c);
                for (int c = 0; c <= t->cursor_col; c++)
                    clear_cell(t, t->cursor_row, c);
            } else if (p0 == 2 || p0 == 3) {
                /* Erase entire screen, home cursor */
                for (int r = 0; r < t->rows; r++)
                    for (int c = 0; c < t->cols; c++)
                        clear_cell(t, r, c);
                t->cursor_row = 0;
                t->cursor_col = 0;
            }
            mark_all_dirty(t);
            break;
        }

        /* ── Erase in line ── */
        case 'K': {
            /*
             * EL — erase in line. Core of readline redraw.
             *
             * readline Up/Down arrow sequence:
             *   \r       → cursor_col = 0
             *   \x1b[K   → EL0: clear col 0 to end (whole line)
             *   prompt   → print new content on clean line
             *
             * Uses clear_cell() which resets bg — critical for
             * preventing old colored text from showing through.
             */
            if (p0 == 0) {
                /* EL0: erase from cursor to end of line */
                for (int c = t->cursor_col; c < t->cols; c++)
                    clear_cell(t, t->cursor_row, c);
            } else if (p0 == 1) {
                /* EL1: erase from start of line to cursor */
                for (int c = 0; c <= t->cursor_col; c++)
                    clear_cell(t, t->cursor_row, c);
            } else if (p0 == 2) {
                /* EL2: erase entire line, cursor does NOT move */
                for (int c = 0; c < t->cols; c++)
                    clear_cell(t, t->cursor_row, c);
            }
            break;
        }

        /* ── Insert / delete lines ── */
        case 'L': {
            /* IL — insert N blank lines at cursor row */
            int n = (p0 < 1) ? 1 : p0;
            for (int i = 0; i < n; i++) {
                Cell *bottom = t->cells[t->rows-1];
                memmove(&t->cells[t->cursor_row+1],
                        &t->cells[t->cursor_row],
                        (t->rows-t->cursor_row-1) * sizeof(Cell*));
                t->cells[t->cursor_row] = bottom;
                for (int c = 0; c < t->cols; c++)
                    clear_cell(t, t->cursor_row, c);
            }
            mark_all_dirty(t);
            break;
        }
        case 'M': {
            /* DL — delete N lines at cursor row */
            int n = (p0 < 1) ? 1 : p0;
            for (int i = 0; i < n; i++) {
                Cell *top = t->cells[t->cursor_row];
                memmove(&t->cells[t->cursor_row],
                        &t->cells[t->cursor_row+1],
                        (t->rows-t->cursor_row-1) * sizeof(Cell*));
                t->cells[t->rows-1] = top;
                for (int c = 0; c < t->cols; c++)
                    clear_cell(t, t->rows-1, c);
            }
            mark_all_dirty(t);
            break;
        }

        /* ── Character operations ── */
        case 'X': {
            /* ECH — erase N characters at cursor */
            int n = (p0 < 1) ? 1 : p0;
            for (int c = t->cursor_col;
                 c < t->cursor_col+n && c < t->cols; c++)
                clear_cell(t, t->cursor_row, c);
            break;
        }

        case 'P': {
            /*
             * DCH — delete N characters at cursor position.
             * Shifts characters after cursor LEFT by N.
             * Fills the tail with blank cells.
             *
             * Example with N=1, cursor at col 3:
             *   Before: a b c [X] e f g _
             *   After:  a b c  e  f g _ _
             *
             * Used by readline when you press the Delete key.
             */
            int n   = (p0 < 1) ? 1 : p0;
            int row = t->cursor_row;
            int col = t->cursor_col;
            for (int c = col; c < t->cols; c++) {
                int src = c + n;
                if (src < t->cols) {
                    t->cells[row][c] = t->cells[row][src];
                    t->cells[row][c].dirty = 1;
                } else {
                    clear_cell(t, row, c);
                }
            }
            break;
        }

        case '@': {
            /*
             * ICH — insert N blank characters at cursor.
             * Shifts existing characters RIGHT by N.
             * Characters shifted past the right edge are lost.
             *
             * Example with N=1, cursor at col 3:
             *   Before: a b c [X] e f g h
             *   After:  a b c  _  X e f g
             *
             * Used by readline when you type in the middle of
             * an existing command line (insert mode).
             */
            int n   = (p0 < 1) ? 1 : p0;
            int row = t->cursor_row;
            int col = t->cursor_col;
            /* Shift right — iterate from right to avoid clobbering */
            for (int c = t->cols-1; c >= col+n; c--) {
                t->cells[row][c] = t->cells[row][c-n];
                t->cells[row][c].dirty = 1;
            }
            /* Clear the newly opened cells */
            for (int c = col; c < col+n && c < t->cols; c++)
                clear_cell(t, row, c);
            break;
        }

        /* ── Mode / status — acknowledge, ignore ── */
        case 'h':
        case 'l':
        case 'r':
        case 'n':
        case 'c':
            break;

        default:
            break;
    }
}


/* ── terminal_process ───────────────────────────────────────── */

void terminal_process(Terminal *t, const char *buf, int len) {
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)buf[i];

        switch (t->state) {

            case STATE_NORMAL:

                if (c == 0x1b) {
                    t->state = STATE_ESCAPE;

                } else if (c == '\r') {
                    /*
                     * CR — carriage return: move to column 0.
                     * readline uses \r before redrawing a line.
                     */
                    t->cursor_col = 0;

                } else if (c == '\n') {
                    /*
                     * LF — line feed: advance row AND reset col=0.
                     *
                     * WHY col=0 on \n:
                     * bash sends \r\n pairs — \r sets col=0, then
                     * \n advances the row. Resetting col=0 again
                     * on \n is harmless (already 0 from \r).
                     *
                     * But programs sending bare \n (no preceding \r)
                     * need the col reset here too — otherwise output
                     * appears at a non-zero column on the new row
                     * causing the path/prompt duplication bug.
                     *
                     * This matches xterm and VTE behavior.
                     */
                    t->cursor_col = 0;
                    t->cursor_row++;
                    if (t->cursor_row >= t->rows) {
                        scroll_up(t);
                        t->cursor_row = t->rows - 1;
                    }

                } else if (c == '\b') {
                    /* BS — backspace: move left one col */
                    if (t->cursor_col > 0) t->cursor_col--;

                } else if (c == '\t') {
                    /* HT — tab: advance to next 8-column boundary */
                    t->cursor_col = (t->cursor_col + 8) & ~7;
                    if (t->cursor_col >= t->cols)
                        t->cursor_col = t->cols - 1;

                } else if (c == 0x07 ||  /* BEL — bell     */
                           c == 0x0e ||  /* SO  — shift out */
                           c == 0x0f ||  /* SI  — shift in  */
                           c == 0x00 ||  /* NUL — null      */
                           c == 0x7f) {  /* DEL — delete    */
                    /* All ignored */

                } else if (c >= 0x20 && c < 0x7f) {
                    /* Printable ASCII */
                    put_char(t, (char)c);

                } else if (c >= 0xa0) {
                    /* High-byte printable — draw placeholder */
                    put_char(t, '?');
                }
                /* 0x80–0x9f: C1 controls — ignore */
                break;

            case STATE_ESCAPE:

                if (c == '[') {
                    /* CSI introducer */
                    t->state      = STATE_CSI;
                    t->params_len = 0;
                    memset(t->params, 0, sizeof(t->params));

                } else if (c == 'c') {
                    /* RIS — full terminal reset */
                    for (int r = 0; r < t->rows; r++)
                        for (int col = 0; col < t->cols; col++)
                            clear_cell(t, r, col);
                    t->cursor_row = 0;
                    t->cursor_col = 0;
                    t->current_fg = DEFAULT_FG;
                    t->current_bg = DEFAULT_BG;
                    t->bold       = 0;
                    t->state      = STATE_NORMAL;

                } else if (c == 'M') {
                    /* RI — reverse index: scroll down */
                    if (t->cursor_row > 0) t->cursor_row--;
                    t->state = STATE_NORMAL;

                } else if (c == '7' || c == '8') {
                    /* DECSC/DECRC — save/restore cursor (ignore) */
                    t->state = STATE_NORMAL;

                } else if (c == '=' || c == '>') {
                    /* DECPAM/DECPNM — keypad mode (ignore) */
                    t->state = STATE_NORMAL;

                } else if (c == '(' || c == ')' ||
                           c == '*' || c == '+') {
                    /* Character set designation (ignore) */
                    t->state = STATE_NORMAL;

                } else {
                    t->state = STATE_NORMAL;
                }
                break;

            case STATE_CSI:

                if ((c >= '0' && c <= '9')
                        || c == ';'
                        || c == '?'   /* DEC private params      */
                        || c == '!'   /* intermediate bytes       */
                        || c == '"'
                        || c == '$'
                        || c == '>'   /* device attribute queries */
                        || c == ' '   /* intermediate space       */
                        || c == '\'') {
                    /* Accumulate parameter/intermediate bytes */
                    if (t->params_len < (int)sizeof(t->params)-1) {
                        t->params[t->params_len++] = (char)c;
                        t->params[t->params_len]   = '\0';
                    }

                } else if (c >= 0x40 && c <= 0x7e) {
                    /* Final byte — dispatch the command */
                    apply_csi(t, (char)c);
                    t->state = STATE_NORMAL;

                } else {
                    /* Unexpected byte — abort */
                    t->state = STATE_NORMAL;
                }
                break;
        }
    }
}


/* ── terminal_resize ────────────────────────────────────────── */

void terminal_resize(Terminal *t, int cols, int rows) {
    if (t->cells) {
        for (int r = 0; r < t->rows; r++) free(t->cells[r]);
        free(t->cells);
        t->cells = NULL;
    }
    t->cols = cols;
    t->rows = rows;

    t->cells = calloc(rows, sizeof(Cell *));
    if (!t->cells) return;

    for (int r = 0; r < rows; r++) {
        t->cells[r] = calloc(cols, sizeof(Cell));
        if (!t->cells[r]) return;
        for (int c = 0; c < cols; c++) {
            t->cells[r][c].ch    = ' ';
            t->cells[r][c].fg    = DEFAULT_FG;
            t->cells[r][c].bg    = DEFAULT_BG;
            t->cells[r][c].dirty = 1;
        }
    }

    if (t->cursor_row >= rows) t->cursor_row = rows-1;
    if (t->cursor_col >= cols) t->cursor_col = cols-1;
    if (t->cursor_row < 0)    t->cursor_row = 0;
    if (t->cursor_col < 0)    t->cursor_col = 0;
}


/* ── Scrollback public API ──────────────────────────────────── */

ScrollbackLine *scrollback_get(Terminal *t, int index) {
    if (index < 0 || index >= t->sb_count) return NULL;
    int pos = (t->sb_head - 1 - index + SCROLLBACK_MAX)
              % SCROLLBACK_MAX;
    return &t->scrollback[pos];
}

Cell *terminal_get_display_row(Terminal *t, int screen_row) {
    if (t->scroll_offset == 0)
        return t->cells[screen_row];

    int total       = t->sb_count + t->rows;
    int view_bottom = total - t->scroll_offset;
    int view_top    = view_bottom - t->rows;
    int line_index  = view_top + screen_row;

    if (line_index < 0 || line_index >= total)
        return NULL;

    if (line_index < t->sb_count) {
        int sb_index = (t->sb_count - 1) - line_index;
        ScrollbackLine *sl = scrollback_get(t, sb_index);
        return sl ? sl->cells : NULL;
    }

    int live_row = line_index - t->sb_count;
    if (live_row >= 0 && live_row < t->rows)
        return t->cells[live_row];
    return NULL;
}