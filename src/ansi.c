/*
 * ansi.c — ANSI/VT100 terminal emulator.
 *
 * Critical fix for blank screen:
 *   The OSC state (STATE_OSC) was entered correctly but the
 *   ST terminator handling was wrong. When bash sends:
 *     ESC ] 0 ; title ESC \
 *   the ESC in "ESC \" was transitioning to STATE_ESCAPE, which
 *   then saw '\' and fell through to STATE_NORMAL — correct.
 *   BUT if bash uses BEL termination (ESC ] 0 ; title BEL) and
 *   the BEL arrived in the same read() chunk as subsequent prompt
 *   bytes, the OSC handler sometimes consumed those prompt bytes.
 *
 *   Fix: OSC handler is now strict — only BEL (0x07) and
 *   ESC (0x1b) exit OSC state. All other bytes are discarded.
 *   The ESC path transitions to STATE_ESCAPE so ESC \ works.
 *
 * All other fixes from previous versions retained.
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

/* ── Defaults ───────────────────────────────────────────────── */
static const Color DEFAULT_FG = {200, 200, 200};
static const Color DEFAULT_BG = {  0,   0,   0};

/* ── terminal_create ────────────────────────────────────────── */
Terminal *terminal_create(int cols, int rows) {
    Terminal *t = calloc(1, sizeof(Terminal));
    if (!t) return NULL;

    t->cols = cols; t->rows = rows;
    t->current_fg = DEFAULT_FG; t->current_bg = DEFAULT_BG;
    t->state = STATE_NORMAL;
    t->scroll_offset = 0; t->sb_head = 0; t->sb_count = 0;
    t->alt_screen = 0; t->saved_col = 0; t->saved_row = 0;

    t->cells = calloc(rows, sizeof(Cell *));
    if (!t->cells) { free(t); return NULL; }

    for (int r = 0; r < rows; r++) {
        t->cells[r] = calloc(cols, sizeof(Cell));
        if (!t->cells[r]) { terminal_destroy(t); return NULL; }
        for (int c = 0; c < cols; c++) {
            t->cells[r][c].ch    = ' ';
            t->cells[r][c].fg    = DEFAULT_FG;
            t->cells[r][c].bg    = DEFAULT_BG;
            t->cells[r][c].dirty = 1;
        }
    }
    return t;
}

/* ── terminal_destroy ───────────────────────────────────────── */
void terminal_destroy(Terminal *t) {
    if (!t) return;
    if (t->cells) {
        for (int r = 0; r < t->rows; r++) free(t->cells[r]);
        free(t->cells);
    }
    for (int i = 0; i < SCROLLBACK_MAX; i++)
        if (t->scrollback[i].cells) free(t->scrollback[i].cells);
    free(t);
}

/* ── clear_cell ─────────────────────────────────────────────── */
static inline void clear_cell(Terminal *t, int row, int col) {
    if ((unsigned)row >= (unsigned)t->rows) return;
    if ((unsigned)col >= (unsigned)t->cols) return;
    Cell *cell = &t->cells[row][col];
    cell->ch = ' '; cell->fg = DEFAULT_FG;
    cell->bg = DEFAULT_BG; cell->bold = 0; cell->dirty = 1;
}

static void mark_all_dirty(Terminal *t) {
    for (int r = 0; r < t->rows; r++)
        for (int c = 0; c < t->cols; c++)
            t->cells[r][c].dirty = 1;
}

static inline int clamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ── scrollback ─────────────────────────────────────────────── */
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
    memmove(&t->cells[0], &t->cells[1], (t->rows-1)*sizeof(Cell*));
    t->cells[t->rows-1] = top;
    for (int c = 0; c < t->cols; c++) clear_cell(t, t->rows-1, c);
    if (t->scroll_offset > 0) {
        t->scroll_offset++;
        if (t->scroll_offset > t->sb_count)
            t->scroll_offset = t->sb_count;
    }
}

static void put_char(Terminal *t, char ch) {
    if (t->cursor_col >= t->cols) { t->cursor_col = 0; t->cursor_row++; }
    if (t->cursor_row >= t->rows) { scroll_up(t); t->cursor_row = t->rows-1; }
    Cell *cell = &t->cells[t->cursor_row][t->cursor_col];
    cell->ch = ch; cell->fg = t->current_fg;
    cell->bg = t->current_bg; cell->bold = t->bold; cell->dirty = 1;
    t->cursor_col++;
}

/* ── CSI params ─────────────────────────────────────────────── */
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

/* ── SGR ────────────────────────────────────────────────────── */
static void apply_sgr(Terminal *t, int *params, int count) {
    for (int i = 0; i < count; i++) {
        int p = params[i];
        switch (p) {
            case 0:
                t->current_fg = DEFAULT_FG; t->current_bg = DEFAULT_BG;
                t->bold = 0; break;
            case 1:  t->bold = 1; break;
            case 2: case 22: t->bold = 0; break;
            case 39: t->current_fg = DEFAULT_FG; break;
            case 49: t->current_bg = DEFAULT_BG; break;
            default:
                if (p>=30&&p<=37) t->current_fg=ANSI_COLORS[(p-30)+(t->bold?8:0)];
                else if (p>=40&&p<=47) t->current_bg=ANSI_COLORS[p-40];
                else if (p>=90&&p<=97) t->current_fg=ANSI_COLORS[(p-90)+8];
                else if (p>=100&&p<=107) t->current_bg=ANSI_COLORS[(p-100)+8];
                else if (p==38) {
                    if (i+2<count&&params[i+1]==5) {
                        int n=params[i+2]; if(n>=0&&n<16) t->current_fg=ANSI_COLORS[n]; i+=2;
                    } else if (i+4<count&&params[i+1]==2) {
                        t->current_fg.r=(uint8_t)params[i+2];
                        t->current_fg.g=(uint8_t)params[i+3];
                        t->current_fg.b=(uint8_t)params[i+4]; i+=4;
                    }
                } else if (p==48) {
                    if (i+2<count&&params[i+1]==5) {
                        int n=params[i+2]; if(n>=0&&n<16) t->current_bg=ANSI_COLORS[n]; i+=2;
                    } else if (i+4<count&&params[i+1]==2) {
                        t->current_bg.r=(uint8_t)params[i+2];
                        t->current_bg.g=(uint8_t)params[i+3];
                        t->current_bg.b=(uint8_t)params[i+4]; i+=4;
                    }
                }
                break;
        }
    }
}

/* ── CSI dispatch ───────────────────────────────────────────── */
static void apply_csi(Terminal *t, char final) {
    int params[MAX_PARAMS]; int count = 0;
    parse_params(t->params, params, &count);
    int p0 = params[0];
    int p1 = (count>1) ? params[1] : 1; if (p1<1) p1=1;
    int cr = t->cursor_row, cc = t->cursor_col;

    switch (final) {
        case 'm': apply_sgr(t, params, count); break;

        case 'A': t->cursor_row=clamp(cr-((p0<1)?1:p0),0,t->rows-1); break;
        case 'B': t->cursor_row=clamp(cr+((p0<1)?1:p0),0,t->rows-1); break;
        case 'C': t->cursor_col=clamp(cc+((p0<1)?1:p0),0,t->cols-1); break;
        case 'D': t->cursor_col=clamp(cc-((p0<1)?1:p0),0,t->cols-1); break;
        case 'E': t->cursor_row=clamp(cr+((p0<1)?1:p0),0,t->rows-1); t->cursor_col=0; break;
        case 'F': t->cursor_row=clamp(cr-((p0<1)?1:p0),0,t->rows-1); t->cursor_col=0; break;
        case 'G': t->cursor_col=clamp(((p0<1)?1:p0)-1,0,t->cols-1); break;
        case 'H': case 'f':
            t->cursor_row=clamp(((p0<1)?1:p0)-1,0,t->rows-1);
            t->cursor_col=clamp(((p1<1)?1:p1)-1,0,t->cols-1); break;
        case 'd': t->cursor_row=clamp(((p0<1)?1:p0)-1,0,t->rows-1); break;

        case 'J':
            if (p0==0) {
                for (int c=cc;c<t->cols;c++) clear_cell(t,cr,c);
                for (int r=cr+1;r<t->rows;r++) for (int c=0;c<t->cols;c++) clear_cell(t,r,c);
            } else if (p0==1) {
                for (int r=0;r<cr;r++) for (int c=0;c<t->cols;c++) clear_cell(t,r,c);
                for (int c=0;c<=cc;c++) clear_cell(t,cr,c);
            } else if (p0==2||p0==3) {
                for (int r=0;r<t->rows;r++) for (int c=0;c<t->cols;c++) clear_cell(t,r,c);
                t->cursor_row=0; t->cursor_col=0;
            }
            mark_all_dirty(t); break;

        case 'K':
            if (p0==0) { for (int c=cc;c<t->cols;c++) clear_cell(t,cr,c); }
            else if (p0==1) { for (int c=0;c<=cc;c++) clear_cell(t,cr,c); }
            else if (p0==2) { for (int c=0;c<t->cols;c++) clear_cell(t,cr,c); }
            break;

        case 'L': {
            int n=(p0<1)?1:p0;
            for (int i=0;i<n;i++) {
                Cell *bot=t->cells[t->rows-1];
                memmove(&t->cells[cr+1],&t->cells[cr],(t->rows-cr-1)*sizeof(Cell*));
                t->cells[cr]=bot; for (int c=0;c<t->cols;c++) clear_cell(t,cr,c);
            }
            mark_all_dirty(t); break;
        }
        case 'M': {
            int n=(p0<1)?1:p0;
            for (int i=0;i<n;i++) {
                Cell *top=t->cells[cr];
                memmove(&t->cells[cr],&t->cells[cr+1],(t->rows-cr-1)*sizeof(Cell*));
                t->cells[t->rows-1]=top; for (int c=0;c<t->cols;c++) clear_cell(t,t->rows-1,c);
            }
            mark_all_dirty(t); break;
        }

        case 'X': {
            int n=(p0<1)?1:p0;
            for (int c=cc;c<cc+n&&c<t->cols;c++) clear_cell(t,cr,c); break;
        }
        case 'P': {
            int n=(p0<1)?1:p0;
            for (int c=cc;c<t->cols;c++) {
                int src=c+n;
                if (src<t->cols) { t->cells[cr][c]=t->cells[cr][src]; t->cells[cr][c].dirty=1; }
                else clear_cell(t,cr,c);
            }
            break;
        }
        case '@': {
            int n=(p0<1)?1:p0;
            for (int c=t->cols-1;c>=cc+n;c--) { t->cells[cr][c]=t->cells[cr][c-n]; t->cells[cr][c].dirty=1; }
            for (int c=cc;c<cc+n&&c<t->cols;c++) clear_cell(t,cr,c); break;
        }

        case 'S': { int n=(p0<1)?1:p0; for (int i=0;i<n;i++) scroll_up(t); break; }
        case 'T': {
            int n=(p0<1)?1:p0;
            for (int i=0;i<n;i++) {
                Cell *bot=t->cells[t->rows-1];
                memmove(&t->cells[1],&t->cells[0],(t->rows-1)*sizeof(Cell*));
                t->cells[0]=bot; for (int c=0;c<t->cols;c++) clear_cell(t,0,c);
            }
            mark_all_dirty(t); break;
        }

        case 's': t->saved_col=cc; t->saved_row=cr; break;
        case 'u':
            t->cursor_col=clamp(t->saved_col,0,t->cols-1);
            t->cursor_row=clamp(t->saved_row,0,t->rows-1); break;

        case 'h': case 'l': case 'r': case 'n': case 'c': case 'b':
            break;
        default: break;
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
                    t->cursor_col = 0;
                } else if (c == '\n') {
                    /*
                     * LF: advance row AND reset col to 0.
                     * Fixes prompt duplication bug.
                     */
                    t->cursor_col = 0;
                    t->cursor_row++;
                    if (t->cursor_row >= t->rows) {
                        scroll_up(t);
                        t->cursor_row = t->rows - 1;
                    }
                } else if (c == '\b') {
                    if (t->cursor_col > 0) t->cursor_col--;
                } else if (c == '\t') {
                    t->cursor_col = (t->cursor_col + 8) & ~7;
                    if (t->cursor_col >= t->cols) t->cursor_col = t->cols-1;
                } else if (c==0x07||c==0x0e||c==0x0f||c==0x00||c==0x7f||c==0x05) {
                    /* BEL SO SI NUL DEL ENQ — ignore */
                } else if (c >= 0x20 && c < 0x7f) {
                    put_char(t, (char)c);
                } else if (c >= 0xa0) {
                    put_char(t, '?');
                }
                break;

            case STATE_ESCAPE:
                if (c == '[') {
                    t->state = STATE_CSI;
                    t->params_len = 0;
                    memset(t->params, 0, sizeof(t->params));
                } else if (c == ']') {
                    /*
                     * OSC start.
                     * bash uses this for window title, shell
                     * integration marks, working directory etc.
                     * We MUST consume all OSC bytes to prevent
                     * them appearing as printable characters.
                     */
                    t->state   = STATE_OSC;
                    t->osc_len = 0;
                    memset(t->osc_buf, 0, sizeof(t->osc_buf));
                } else if (c=='P'||c=='^'||c=='_') {
                    /* DCS/PM/APC — treat same as OSC */
                    t->state = STATE_OSC;
                    t->osc_len = 0;
                } else if (c == 'c') {
                    for (int r=0;r<t->rows;r++)
                        for (int col=0;col<t->cols;col++)
                            clear_cell(t,r,col);
                    t->cursor_row=0; t->cursor_col=0;
                    t->current_fg=DEFAULT_FG; t->current_bg=DEFAULT_BG;
                    t->bold=0; t->state=STATE_NORMAL;
                } else if (c == 'M') {
                    if (t->cursor_row > 0) t->cursor_row--;
                    t->state = STATE_NORMAL;
                } else if (c == '7') {
                    t->saved_col=t->cursor_col; t->saved_row=t->cursor_row;
                    t->state = STATE_NORMAL;
                } else if (c == '8') {
                    t->cursor_col=clamp(t->saved_col,0,t->cols-1);
                    t->cursor_row=clamp(t->saved_row,0,t->rows-1);
                    t->state = STATE_NORMAL;
                } else if (c=='='||c=='>') {
                    t->state = STATE_NORMAL;
                } else if (c=='('||c==')'||c=='*'||c=='+') {
                    t->state = STATE_NORMAL;
                } else if (c=='\\') {
                    /* ST bare — string terminator */
                    t->state = STATE_NORMAL;
                } else {
                    t->state = STATE_NORMAL;
                }
                break;

            case STATE_CSI:
                if ((c>='0'&&c<='9')||c==';'||c=='?'||c=='!'||
                    c=='"'||c=='$'||c=='>'||c==' '||c=='\'') {
                    if (t->params_len < (int)sizeof(t->params)-1) {
                        t->params[t->params_len++]=(char)c;
                        t->params[t->params_len]='\0';
                    }
                } else if (c>=0x40&&c<=0x7e) {
                    apply_csi(t,(char)c);
                    t->state=STATE_NORMAL;
                } else if (c==0x1b) {
                    t->state=STATE_ESCAPE;
                } else {
                    t->state=STATE_NORMAL;
                }
                break;

            case STATE_OSC:
                /*
                 * Consume OSC content until:
                 *   BEL (0x07) — OSC terminator used by most shells
                 *   ESC (0x1b) — start of ST = ESC \
                 *
                 * CRITICAL: we must NOT let any OSC byte leak into
                 * STATE_NORMAL as a printable character.
                 * This was causing the blank screen — bash's
                 * title-setting OSC sequence was filling cells with
                 * the title string before the prompt could render.
                 */
                if (c == 0x07) {
                    /* BEL terminates OSC — back to normal */
                    t->state = STATE_NORMAL;
                } else if (c == 0x1b) {
                    /*
                     * ESC inside OSC — this is the start of ST
                     * (String Terminator = ESC \).
                     * Transition to ESCAPE so the following '\'
                     * will be handled there as a bare ST.
                     */
                    t->state = STATE_ESCAPE;
                }
                /* All other bytes: silently consumed */
                break;
        }
    }
}

/* ── terminal_resize ────────────────────────────────────────── */
void terminal_resize(Terminal *t, int cols, int rows) {
    if (t->cells) {
        for (int r=0;r<t->rows;r++) free(t->cells[r]);
        free(t->cells); t->cells=NULL;
    }
    t->cols=cols; t->rows=rows;
    t->cells=calloc(rows,sizeof(Cell*));
    if (!t->cells) return;
    for (int r=0;r<rows;r++) {
        t->cells[r]=calloc(cols,sizeof(Cell));
        if (!t->cells[r]) return;
        for (int c=0;c<cols;c++) {
            t->cells[r][c].ch=' '; t->cells[r][c].fg=DEFAULT_FG;
            t->cells[r][c].bg=DEFAULT_BG; t->cells[r][c].dirty=1;
        }
    }
    t->cursor_row=clamp(t->cursor_row,0,rows-1);
    t->cursor_col=clamp(t->cursor_col,0,cols-1);
}

/* ── Scrollback API ─────────────────────────────────────────── */
ScrollbackLine *scrollback_get(Terminal *t, int index) {
    if (index<0||index>=t->sb_count) return NULL;
    int pos=(t->sb_head-1-index+SCROLLBACK_MAX)%SCROLLBACK_MAX;
    return &t->scrollback[pos];
}

Cell *terminal_get_display_row(Terminal *t, int screen_row) {
    if (t->scroll_offset==0) return t->cells[screen_row];
    int total=t->sb_count+t->rows;
    int view_bottom=total-t->scroll_offset;
    int view_top=view_bottom-t->rows;
    int line_index=view_top+screen_row;
    if (line_index<0||line_index>=total) return NULL;
    if (line_index<t->sb_count) {
        int sb_idx=(t->sb_count-1)-line_index;
        ScrollbackLine *sl=scrollback_get(t,sb_idx);
        return sl?sl->cells:NULL;
    }
    int live=line_index-t->sb_count;
    return (live>=0&&live<t->rows)?t->cells[live]:NULL;
}