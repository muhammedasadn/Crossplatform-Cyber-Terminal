#include "ansi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Default colors: light gray on black */
static const Color DEFAULT_FG = {200, 200, 200};
static const Color DEFAULT_BG = {  0,   0,   0};

/*
 * terminal_create — allocates the cell grid on the heap.
 *
 * We use malloc() to allocate memory dynamically because
 * the grid size isn't known at compile time — it depends
 * on window size and font size chosen at runtime.
 *
 * malloc(n) returns a pointer to n bytes of uninitialized memory.
 * calloc(n, size) returns n*size bytes all zeroed — safer for grids.
 */
Terminal *terminal_create(int cols, int rows) {
    Terminal *t = calloc(1, sizeof(Terminal));
    if (!t) return NULL;

    t->cols = cols;
    t->rows = rows;
    t->current_fg = DEFAULT_FG;
    t->current_bg = DEFAULT_BG;
    t->state = STATE_NORMAL;

    /*
     * Allocate the 2D cell grid.
     * We allocate an array of ROW pointers, then for each
     * row we allocate an array of COLS Cell structs.
     *
     * Memory layout:
     *   t->cells        → [ptr row0] [ptr row1] [ptr row2] ...
     *   t->cells[0]     → [Cell][Cell][Cell]...  (cols cells)
     *   t->cells[1]     → [Cell][Cell][Cell]...
     */
    t->cells = calloc(rows, sizeof(Cell *));
    if (!t->cells) { free(t); return NULL; }

    for (int r = 0; r < rows; r++) {
        t->cells[r] = calloc(cols, sizeof(Cell));
        if (!t->cells[r]) { terminal_destroy(t); return NULL; }

        /* Initialize every cell with space + default colors */
        for (int c = 0; c < cols; c++) {
            t->cells[r][c].ch = ' ';
            t->cells[r][c].fg = DEFAULT_FG;
            t->cells[r][c].bg = DEFAULT_BG;
        }
    }

    return t;
}

/*
 * terminal_destroy — free every allocation in reverse order.
 * In C, you must manually free everything you malloc.
 * Forgetting this causes memory leaks.
 */
void terminal_destroy(Terminal *t) {
    if (!t) return;
    if (t->cells) {
        for (int r = 0; r < t->rows; r++) {
            free(t->cells[r]);   /* free each row array */
        }
        free(t->cells);          /* free the pointer array */
    }
    free(t);                     /* free the Terminal struct itself */
}

/* ── Internal helpers ────────────────────────────────────────── */

/*
 * scroll_up — shifts all rows up by one, clears the bottom row.
 * Called when the cursor moves past the last row.
 * memmove handles overlapping memory safely (unlike memcpy).
 */
static void scroll_up(Terminal *t) {
    /* Save pointer to row 0 — we'll reuse it as the new last row */
    Cell *top_row = t->cells[0];

    /* Shift all row pointers up by one position */
    memmove(&t->cells[0], &t->cells[1], (t->rows - 1) * sizeof(Cell *));

    /* Put the old top row at the bottom and clear it */
    t->cells[t->rows - 1] = top_row;
    for (int c = 0; c < t->cols; c++) {
        t->cells[t->rows - 1][c].ch    = ' ';
        t->cells[t->rows - 1][c].fg    = DEFAULT_FG;
        t->cells[t->rows - 1][c].bg    = DEFAULT_BG;
        t->cells[t->rows - 1][c].dirty = 1;
    }
}

/* Mark every cell dirty so it gets redrawn */
static void mark_all_dirty(Terminal *t) {
    for (int r = 0; r < t->rows; r++)
        for (int c = 0; c < t->cols; c++)
            t->cells[r][c].dirty = 1;
}

/* Put a character at current cursor position and advance */
static void put_char(Terminal *t, char ch) {
    if (t->cursor_col >= t->cols) {
        /* Reached end of line — wrap to next line */
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

/*
 * parse_params — splits "1;32;0" into integers [1, 32, 0].
 *
 * ANSI parameters are separated by semicolons.
 * We parse them into a small integer array.
 * strtol() converts a string to a long integer.
 */
static void parse_params(const char *raw, int *out, int *count) {
    *count = 0;
    const char *p = raw;

    while (*p && *count < MAX_PARAMS) {
        /* strtol(str, &end, base) parses an integer.
         * end is updated to point past the parsed number. */
        char *end;
        out[(*count)++] = (int)strtol(p, &end, 10);
        if (*end == ';') end++;  /* skip the semicolon */
        if (end == p) break;     /* no progress — stop */
        p = end;
    }

    /* Always have at least one param (0 = default) */
    if (*count == 0) {
        out[0] = 0;
        *count  = 1;
    }
}

/*
 * apply_sgr — handles \x1b[...m sequences.
 * SGR = Select Graphic Rendition.
 * This is how all colors and bold/italic/underline work.
 */
static void apply_sgr(Terminal *t, int *params, int count) {
    for (int i = 0; i < count; i++) {
        int p = params[i];

        if (p == 0) {
            /* Reset everything to defaults */
            t->current_fg = DEFAULT_FG;
            t->current_bg = DEFAULT_BG;
            t->bold = 0;

        } else if (p == 1) {
            t->bold = 1;

        } else if (p == 2) {
            t->bold = 0;  /* Dim / normal weight */

        } else if (p >= 30 && p <= 37) {
            /* Standard foreground colors 30–37 */
            int idx = p - 30;
            if (t->bold) idx += 8;  /* Bold = bright variant */
            t->current_fg = ANSI_COLORS[idx];

        } else if (p == 39) {
            /* Default foreground */
            t->current_fg = DEFAULT_FG;

        } else if (p >= 40 && p <= 47) {
            /* Standard background colors 40–47 */
            t->current_bg = ANSI_COLORS[p - 40];

        } else if (p == 49) {
            /* Default background */
            t->current_bg = DEFAULT_BG;

        } else if (p >= 90 && p <= 97) {
            /* Bright foreground colors 90–97 */
            t->current_fg = ANSI_COLORS[(p - 90) + 8];

        } else if (p >= 100 && p <= 107) {
            /* Bright background colors 100–107 */
            t->current_bg = ANSI_COLORS[(p - 100) + 8];

        } else if (p == 38) {
            /*
             * 256-color or truecolor foreground.
             * Format: 38;5;n (256-color) or 38;2;r;g;b (truecolor)
             */
            if (i + 1 < count && params[i+1] == 5 && i + 2 < count) {
                /* 256-color mode — simplified: map to nearest 16 */
                int n = params[i+2];
                if (n < 16) t->current_fg = ANSI_COLORS[n];
                i += 2;
            } else if (i + 1 < count && params[i+1] == 2 && i + 4 < count) {
                /* True color RGB */
                t->current_fg.r = params[i+2];
                t->current_fg.g = params[i+3];
                t->current_fg.b = params[i+4];
                i += 4;
            }

        } else if (p == 48) {
            /* 256-color or truecolor background */
            if (i + 1 < count && params[i+1] == 5 && i + 2 < count) {
                int n = params[i+2];
                if (n < 16) t->current_bg = ANSI_COLORS[n];
                i += 2;
            } else if (i + 1 < count && params[i+1] == 2 && i + 4 < count) {
                t->current_bg.r = params[i+2];
                t->current_bg.g = params[i+3];
                t->current_bg.b = params[i+4];
                i += 4;
            }
        }
    }
}

/*
 * apply_csi — dispatches a complete CSI sequence.
 * Called when we see the final letter of an escape sequence.
 * The final letter tells us WHAT to do.
 */
static void apply_csi(Terminal *t, char final) {
    int params[MAX_PARAMS];
    int count = 0;
    parse_params(t->params, params, &count);

    int p0 = params[0];  /* First param, 0 if absent */
    int p1 = (count > 1) ? params[1] : 1;

    switch (final) {

        case 'm':
            /* SGR — colors and text attributes */
            apply_sgr(t, params, count);
            break;

        case 'A':
            /* Cursor up N rows */
            t->cursor_row -= (p0 < 1 ? 1 : p0);
            if (t->cursor_row < 0) t->cursor_row = 0;
            break;

        case 'B':
            /* Cursor down N rows */
            t->cursor_row += (p0 < 1 ? 1 : p0);
            if (t->cursor_row >= t->rows) t->cursor_row = t->rows - 1;
            break;

        case 'C':
            /* Cursor right N cols */
            t->cursor_col += (p0 < 1 ? 1 : p0);
            if (t->cursor_col >= t->cols) t->cursor_col = t->cols - 1;
            break;

        case 'D':
            /* Cursor left N cols */
            t->cursor_col -= (p0 < 1 ? 1 : p0);
            if (t->cursor_col < 0) t->cursor_col = 0;
            break;

        case 'H':
        case 'f':
            /*
             * Cursor position: \x1b[row;colH
             * ANSI rows/cols are 1-based, our array is 0-based.
             * So we subtract 1.
             */
            t->cursor_row = (p0 < 1 ? 1 : p0) - 1;
            t->cursor_col = (p1 < 1 ? 1 : p1) - 1;
            if (t->cursor_row >= t->rows) t->cursor_row = t->rows - 1;
            if (t->cursor_col >= t->cols) t->cursor_col = t->cols - 1;
            break;

        case 'J':
            /* Erase in display */
            if (p0 == 0) {
                /* Clear from cursor to end of screen */
                for (int c = t->cursor_col; c < t->cols; c++) {
                    t->cells[t->cursor_row][c].ch = ' ';
                    t->cells[t->cursor_row][c].dirty = 1;
                }
                for (int r = t->cursor_row + 1; r < t->rows; r++)
                    for (int c = 0; c < t->cols; c++) {
                        t->cells[r][c].ch = ' ';
                        t->cells[r][c].dirty = 1;
                    }
            } else if (p0 == 2 || p0 == 3) {
                /* Clear entire screen */
                for (int r = 0; r < t->rows; r++)
                    for (int c = 0; c < t->cols; c++) {
                        t->cells[r][c].ch = ' ';
                        t->cells[r][c].dirty = 1;
                    }
                t->cursor_row = 0;
                t->cursor_col = 0;
            }
            mark_all_dirty(t);
            break;

        case 'K':
            /* Erase in line */
            if (p0 == 0) {
                /* Clear from cursor to end of line */
                for (int c = t->cursor_col; c < t->cols; c++) {
                    t->cells[t->cursor_row][c].ch = ' ';
                    t->cells[t->cursor_row][c].dirty = 1;
                }
            } else if (p0 == 1) {
                /* Clear from start of line to cursor */
                for (int c = 0; c <= t->cursor_col; c++) {
                    t->cells[t->cursor_row][c].ch = ' ';
                    t->cells[t->cursor_row][c].dirty = 1;
                }
            } else if (p0 == 2) {
                /* Clear entire line */
                for (int c = 0; c < t->cols; c++) {
                    t->cells[t->cursor_row][c].ch = ' ';
                    t->cells[t->cursor_row][c].dirty = 1;
                }
            }
            break;

        case 'l':
        case 'h':
            /* Mode set/reset — e.g. \x1b[?25h shows cursor.
             * We acknowledge but don't fully implement these yet. */
            break;

        default:
            /* Unknown sequence — silently ignore */
            break;
    }
}

/*
 * terminal_process — the main parser.
 * Call this every time you get bytes from the PTY.
 * It feeds bytes one at a time through the state machine.
 */
void terminal_process(Terminal *t, const char *buf, int len) {
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)buf[i];

        switch (t->state) {

            case STATE_NORMAL:
                if (c == 0x1b) {
                    /* ESC byte — start of escape sequence */
                    t->state = STATE_ESCAPE;

                } else if (c == '\r') {
                    t->cursor_col = 0;

                } else if (c == '\n') {
                    t->cursor_row++;
                    if (t->cursor_row >= t->rows) {
                        scroll_up(t);
                        t->cursor_row = t->rows - 1;
                    }

                } else if (c == '\b') {
                    if (t->cursor_col > 0) t->cursor_col--;

                } else if (c == '\t') {
                    /* Tab: advance to next 8-column boundary */
                    t->cursor_col = (t->cursor_col + 8) & ~7;
                    if (t->cursor_col >= t->cols)
                        t->cursor_col = t->cols - 1;

                } else if (c >= 32 && c < 127) {
                    put_char(t, (char)c);

                }
                /* Other control chars (BEL, etc.) — ignore */
                break;

            case STATE_ESCAPE:
                if (c == '[') {
                    /* CSI introducer — start collecting params */
                    t->state = STATE_CSI;
                    t->params_len = 0;
                    memset(t->params, 0, sizeof(t->params));
                } else if (c == 'c') {
                    /* Full reset — rare but handle it */
                    t->state = STATE_NORMAL;
                } else {
                    /* Unrecognized escape — go back to normal */
                    t->state = STATE_NORMAL;
                }
                break;

            case STATE_CSI:
                if ((c >= '0' && c <= '9') || c == ';' || c == '?') {
                    /* Parameter character — accumulate it */
                    if (t->params_len < (int)sizeof(t->params) - 1) {
                        t->params[t->params_len++] = (char)c;
                        t->params[t->params_len]   = '\0';
                    }
                } else if (c >= 0x40 && c <= 0x7e) {
                    /*
                     * Final byte (any letter A-Z, a-z, @).
                     * This completes the escape sequence.
                     * Apply it and return to NORMAL state.
                     */
                    apply_csi(t, (char)c);
                    t->state = STATE_NORMAL;
                } else {
                    /* Unexpected byte — abort sequence */
                    t->state = STATE_NORMAL;
                }
                break;
        }
    }
}

/*
 * terminal_resize — rebuild the grid at a new size.
 * Called when the user drags the window to a new size.
 */
void terminal_resize(Terminal *t, int cols, int rows) {
    /* Free old grid */
    for (int r = 0; r < t->rows; r++) free(t->cells[r]);
    free(t->cells);

    /* Rebuild at new size */
    t->cols = cols;
    t->rows = rows;
    t->cells = calloc(rows, sizeof(Cell *));
    for (int r = 0; r < rows; r++) {
        t->cells[r] = calloc(cols, sizeof(Cell));
        for (int c = 0; c < cols; c++) {
            t->cells[r][c].ch = ' ';
            t->cells[r][c].fg = DEFAULT_FG;
            t->cells[r][c].bg = DEFAULT_BG;
        }
    }

    /* Reset cursor to safe position */
    if (t->cursor_row >= rows) t->cursor_row = rows - 1;
    if (t->cursor_col >= cols) t->cursor_col = cols - 1;
}