#ifndef ANSI_H
#define ANSI_H

#include <stdint.h>

/* Maximum parameters in one escape sequence e.g. \x1b[1;32m has 2 */
#define MAX_PARAMS 16

/* ANSI 16 base colors mapped to RGB */
typedef struct {
    uint8_t r, g, b;
} Color;

/*
 * A single cell on the terminal grid.
 * Every position on screen is one Cell.
 */
typedef struct {
    char    ch;         /* The character to display          */
    Color   fg;         /* Foreground (text) color           */
    Color   bg;         /* Background color                  */
    int     bold;       /* 1 = bold text                     */
    int     dirty;      /* 1 = needs redrawing this frame    */
} Cell;

/* Parser states */
typedef enum {
    STATE_NORMAL,   /* Reading regular characters            */
    STATE_ESCAPE,   /* Just saw \x1b                         */
    STATE_CSI       /* Saw \x1b[ — collecting parameters     */
} ParserState;

/*
 * The full terminal state.
 * Owns the cell grid, cursor position, and parser state.
 */
typedef struct {
    Cell         **cells;       /* 2D grid: cells[row][col]  */
    int            cols;
    int            rows;
    int            cursor_col;
    int            cursor_row;
    Color          current_fg;  /* Active foreground color   */
    Color          current_bg;  /* Active background color   */
    int            bold;        /* Active bold flag           */

    /* Parser internals */
    ParserState    state;
    char           params[64];  /* Raw param string e.g "1;32" */
    int            params_len;
} Terminal;

/* The 16 standard ANSI colors (matches most terminal themes) */
static const Color ANSI_COLORS[16] = {
    {  0,   0,   0},  /* 0  Black   */
    {170,   0,   0},  /* 1  Red     */
    {  0, 170,   0},  /* 2  Green   */
    {170, 170,   0},  /* 3  Yellow  */
    {  0,   0, 170},  /* 4  Blue    */
    {170,   0, 170},  /* 5  Magenta */
    {  0, 170, 170},  /* 6  Cyan    */
    {170, 170, 170},  /* 7  White   */
    { 85,  85,  85},  /* 8  Bright Black  (dark gray) */
    {255,  85,  85},  /* 9  Bright Red    */
    { 85, 255,  85},  /* 10 Bright Green  */
    {255, 255,  85},  /* 11 Bright Yellow */
    { 85,  85, 255},  /* 12 Bright Blue   */
    {255,  85, 255},  /* 13 Bright Magenta*/
    { 85, 255, 255},  /* 14 Bright Cyan   */
    {255, 255, 255},  /* 15 Bright White  */
};

/* Function declarations */
Terminal *terminal_create(int cols, int rows);
void      terminal_destroy(Terminal *t);
void      terminal_process(Terminal *t, const char *buf, int len);
void      terminal_resize(Terminal *t, int cols, int rows);

#endif /* ANSI_H */