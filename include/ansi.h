#ifndef ANSI_H
#define ANSI_H

/*
 * ansi.h — Terminal emulation types and public API.
 *
 * Defines everything needed for the ANSI/VT100 parser:
 *   - Color (24-bit RGB)
 *   - Cell (one character position on screen)
 *   - ScrollbackLine (one saved history line)
 *   - ParserState (3-state FSM)
 *   - Terminal (owns the grid, parser state, scrollback)
 *
 * Include this in any file that reads or writes terminal state.
 */

#include <stdint.h>
#include <stdlib.h>

/* ── Constants ──────────────────────────────────────────────── */

/* Total ASCII codepoints handled (0–127) */
#define GLYPH_COUNT     128

/* Max parameters in one CSI sequence e.g. \x1b[1;32m has 2 */
#define MAX_PARAMS      16

/* Max lines kept in scrollback ring buffer */
#define SCROLLBACK_MAX  5000


/* ── Color ──────────────────────────────────────────────────── */

typedef struct {
    uint8_t r, g, b;
} Color;

/*
 * Standard 16 ANSI colors.
 * Indices 0–7  = normal colors (SGR 30–37 fg, 40–47 bg)
 * Indices 8–15 = bright colors (SGR 90–97 fg, 100–107 bg)
 */
static const Color ANSI_COLORS[16] = {
    {  0,   0,   0},  /*  0 Black          */
    {170,   0,   0},  /*  1 Red            */
    {  0, 170,   0},  /*  2 Green          */
    {170, 170,   0},  /*  3 Yellow         */
    {  0,   0, 170},  /*  4 Blue           */
    {170,   0, 170},  /*  5 Magenta        */
    {  0, 170, 170},  /*  6 Cyan           */
    {170, 170, 170},  /*  7 White          */
    { 85,  85,  85},  /*  8 Bright Black   */
    {255,  85,  85},  /*  9 Bright Red     */
    { 85, 255,  85},  /* 10 Bright Green   */
    {255, 255,  85},  /* 11 Bright Yellow  */
    { 85,  85, 255},  /* 12 Bright Blue    */
    {255,  85, 255},  /* 13 Bright Magenta */
    { 85, 255, 255},  /* 14 Bright Cyan    */
    {255, 255, 255},  /* 15 Bright White   */
};


/* ── Cell ───────────────────────────────────────────────────── */

/*
 * Cell — one character position on the terminal grid.
 *
 * The entire screen is cells[rows][cols].
 * Each cell is completely independent — it stores its own
 * character, foreground color, background color, and bold flag.
 * This is how ANSI colors work: every cell knows its own colors.
 *
 * dirty = 1 means this cell changed since the last frame.
 * Used by the dirty-cell optimization in Module 11 to skip
 * redrawing cells that haven't changed.
 */
typedef struct {
    char  ch;       /* Character to display (printable ASCII)   */
    Color fg;       /* Foreground (text) color                  */
    Color bg;       /* Background color                         */
    int   bold;     /* 1 = bold / bright weight                 */
    int   dirty;    /* 1 = changed since last render            */
} Cell;


/* ── ScrollbackLine ─────────────────────────────────────────── */

/*
 * ScrollbackLine — one saved line of terminal history.
 *
 * When a line scrolls off the top of the visible screen,
 * scroll_up() deep-copies it here before overwriting.
 * We save the full Cell array (not just characters) so colors
 * are preserved when the user scrolls back through history.
 *
 * cols is saved per-line because the terminal may have been
 * resized between when this line was captured and now.
 */
typedef struct {
    Cell *cells;   /* Heap-allocated array of 'cols' Cell structs */
    int   cols;    /* Column count when this line was captured    */
} ScrollbackLine;


/* ── ParserState ────────────────────────────────────────────── */

/*
 * The ANSI parser is a 3-state finite state machine (FSM).
 *
 * STATE_NORMAL  → regular character → write to grid
 *              → \x1b received     → switch to STATE_ESCAPE
 *
 * STATE_ESCAPE  → '[' received     → switch to STATE_CSI
 *              → other             → handle or ignore, back to NORMAL
 *
 * STATE_CSI     → digit/semicolon/'?' → accumulate parameter bytes
 *              → letter (0x40-0x7E)   → apply command, back to NORMAL
 *              → unexpected byte      → abort, back to NORMAL
 */
typedef enum {
    STATE_NORMAL,   /* Reading regular printable characters      */
    STATE_ESCAPE,   /* Just received ESC byte (0x1b)             */
    STATE_CSI       /* Inside CSI sequence, collecting params    */
} ParserState;


/* ── Terminal ───────────────────────────────────────────────── */

/*
 * Terminal — complete state of one terminal session.
 *
 * Owns:
 *   cells[][]     — the visible character grid
 *   cursor        — current draw position
 *   current_fg/bg — active colors for new characters
 *   bold          — active bold flag
 *   state/params  — ANSI parser FSM state
 *   scrollback[]  — ring buffer of historical lines
 *   scroll_offset — how many lines we've scrolled up (0=live)
 *
 * One Terminal per PTY session. When tabs have split panes,
 * each leaf Pane owns one Terminal independently.
 */
typedef struct {

    /* ── Visible grid ── */
    Cell **cells;        /* 2D array: cells[row][col]             */
    int    cols;         /* Current width in characters           */
    int    rows;         /* Current height in characters          */

    /* ── Cursor position ── */
    int    cursor_col;   /* 0-based column                        */
    int    cursor_row;   /* 0-based row                           */

    /* ── Active drawing attributes ── */
    Color  current_fg;   /* Foreground color for new chars        */
    Color  current_bg;   /* Background color for new chars        */
    int    bold;         /* 1 if bold mode is active              */

    /* ── ANSI parser state ── */
    ParserState  state;          /* Current FSM state             */
    char         params[64];     /* Raw CSI parameter bytes       */
    int          params_len;     /* Bytes written into params[]   */

    /* ── Scrollback ring buffer ── */
    ScrollbackLine scrollback[SCROLLBACK_MAX];
    int            sb_head;      /* Next write slot (wraps)       */
    int            sb_count;     /* Lines stored so far           */

    /* ── Viewport ── */
    int            scroll_offset; /* 0=live, N=scrolled up N lines */

} Terminal;


/* ── Public API ─────────────────────────────────────────────── */

/*
 * terminal_create — allocate and initialize a Terminal.
 * Returns heap pointer. Caller must call terminal_destroy().
 */
Terminal *terminal_create(int cols, int rows);

/*
 * terminal_destroy — free all memory owned by the Terminal.
 * Always call before closing a tab or pane.
 */
void terminal_destroy(Terminal *t);

/*
 * terminal_process — feed raw PTY bytes into the ANSI parser.
 * Call this every frame after pty_read() returns data.
 * Updates the cell grid and cursor position in place.
 */
void terminal_process(Terminal *t, const char *buf, int len);

/*
 * terminal_resize — rebuild the cell grid at a new size.
 * Call when the SDL2 window or pane rect changes size.
 * Also call pty_resize() afterwards to notify the shell.
 */
void terminal_resize(Terminal *t, int cols, int rows);

/*
 * scrollback_get — retrieve a stored line by recency index.
 * index 0 = most recently saved line (just scrolled off top).
 * Returns NULL if index is out of range.
 */
ScrollbackLine *scrollback_get(Terminal *t, int index);

/*
 * terminal_get_display_row — returns the Cell array to render
 * for a given screen row, accounting for scroll_offset.
 *
 * scroll_offset == 0  → returns live cells[row]
 * scroll_offset == N  → returns historical line from scrollback
 *
 * Returns NULL for rows with no content (render as blank).
 * Use this in the render loop instead of t->cells[row] directly.
 */
Cell *terminal_get_display_row(Terminal *t, int screen_row);


#endif /* ANSI_H */