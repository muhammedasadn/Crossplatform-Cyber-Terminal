/*
 * tools.c — Built-in tool launcher for cterm.
 *
 * Tools are registered at startup with name, command, and args.
 * The launcher overlay appears on Ctrl+P, lets the user search
 * and select a tool, then spawns it in a new tab via PTY.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "tools.h"
#include "tabs.h"
#include "pane.h"
#include "font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>


/* ── tools_init ─────────────────────────────────────────────── */

void tools_init(ToolManager *tm) {
    memset(tm, 0, sizeof(ToolManager));
    int i = 0;

    /* btop */
    strncpy(tm->tools[i].name,    "btop",    sizeof(tm->tools[i].name)    - 1);
    strncpy(tm->tools[i].desc,    "Interactive system monitor (CPU/RAM)",
            sizeof(tm->tools[i].desc) - 1);
    strncpy(tm->tools[i].command, "btop",    sizeof(tm->tools[i].command) - 1);
    tm->tools[i].args[0] = tm->tools[i].command;
    tm->tools[i].args[1] = NULL;
    tm->tools[i].new_tab = 1;
    i++;

    /* htop */
    strncpy(tm->tools[i].name,    "htop",    sizeof(tm->tools[i].name)    - 1);
    strncpy(tm->tools[i].desc,    "Interactive process viewer",
            sizeof(tm->tools[i].desc) - 1);
    strncpy(tm->tools[i].command, "htop",    sizeof(tm->tools[i].command) - 1);
    tm->tools[i].args[0] = tm->tools[i].command;
    tm->tools[i].args[1] = NULL;
    tm->tools[i].new_tab = 1;
    i++;

    /* vim */
    strncpy(tm->tools[i].name,    "vim",     sizeof(tm->tools[i].name)    - 1);
    strncpy(tm->tools[i].desc,    "Vi IMproved text editor",
            sizeof(tm->tools[i].desc) - 1);
    strncpy(tm->tools[i].command, "vim",     sizeof(tm->tools[i].command) - 1);
    tm->tools[i].args[0] = tm->tools[i].command;
    tm->tools[i].args[1] = NULL;
    tm->tools[i].new_tab = 1;
    i++;

    /* nano */
    strncpy(tm->tools[i].name,    "nano",    sizeof(tm->tools[i].name)    - 1);
    strncpy(tm->tools[i].desc,    "Simple terminal text editor",
            sizeof(tm->tools[i].desc) - 1);
    strncpy(tm->tools[i].command, "nano",    sizeof(tm->tools[i].command) - 1);
    tm->tools[i].args[0] = tm->tools[i].command;
    tm->tools[i].args[1] = NULL;
    tm->tools[i].new_tab = 1;
    i++;

    /* nmap */
    strncpy(tm->tools[i].name,    "nmap",    sizeof(tm->tools[i].name)    - 1);
    strncpy(tm->tools[i].desc,    "Network exploration and port scanner",
            sizeof(tm->tools[i].desc) - 1);
    strncpy(tm->tools[i].command, "nmap",    sizeof(tm->tools[i].command) - 1);
    tm->tools[i].args[0] = tm->tools[i].command;
    tm->tools[i].args[1] = "--help";
    tm->tools[i].args[2] = NULL;
    tm->tools[i].new_tab = 1;
    i++;

    /* python3 */
    strncpy(tm->tools[i].name,    "python3", sizeof(tm->tools[i].name)    - 1);
    strncpy(tm->tools[i].desc,    "Python 3 interactive interpreter",
            sizeof(tm->tools[i].desc) - 1);
    strncpy(tm->tools[i].command, "python3", sizeof(tm->tools[i].command) - 1);
    tm->tools[i].args[0] = tm->tools[i].command;
    tm->tools[i].args[1] = NULL;
    tm->tools[i].new_tab = 1;
    i++;

    /* top */
    strncpy(tm->tools[i].name,    "top",     sizeof(tm->tools[i].name)    - 1);
    strncpy(tm->tools[i].desc,    "Classic Unix process monitor",
            sizeof(tm->tools[i].desc) - 1);
    strncpy(tm->tools[i].command, "top",     sizeof(tm->tools[i].command) - 1);
    tm->tools[i].args[0] = tm->tools[i].command;
    tm->tools[i].args[1] = NULL;
    tm->tools[i].new_tab = 1;
    i++;

    /* bash */
    strncpy(tm->tools[i].name,    "bash",    sizeof(tm->tools[i].name)    - 1);
    strncpy(tm->tools[i].desc,    "Open a new bash shell session",
            sizeof(tm->tools[i].desc) - 1);
    strncpy(tm->tools[i].command, "/bin/bash", sizeof(tm->tools[i].command) - 1);
    tm->tools[i].args[0] = tm->tools[i].command;
    tm->tools[i].args[1] = NULL;
    tm->tools[i].new_tab = 1;
    i++;

    tm->count = i;
    printf("ToolManager: %d tools registered.\n", tm->count);
}


/* ── tools_launcher_open / close ────────────────────────────── */

void tools_launcher_open(ToolManager *tm) {
    tm->launcher.visible    = 1;
    tm->launcher.selected   = 0;
    tm->launcher.search[0]  = '\0';
    tm->launcher.search_len = 0;
}

void tools_launcher_close(ToolManager *tm) {
    tm->launcher.visible = 0;
}


/* ── tools_launch ───────────────────────────────────────────── */

void tools_launch(ToolDef *tool, void *tabmgr_ptr,
                  int cols, int rows) {
    TabManager *tm = (TabManager *)tabmgr_ptr;

    if (tabs_new(tm, cols, rows) != 0) {
        fprintf(stderr, "tools_launch: failed to open tab\n");
        return;
    }

    /* Give bash time to start before sending the command */
    usleep(150000);

    Tab  *tab = tabs_get_active(tm);
    Pane *fp  = pane_get_focused(tab->root);

    if (!fp) {
        fprintf(stderr, "tools_launch: no focused pane\n");
        return;
    }

    /* Build command string */
    char cmd[256] = {0};
    strncat(cmd, tool->command, sizeof(cmd) - 2);

    for (int i = 1; tool->args[i] != NULL && i < MAX_TOOL_ARGS; i++) {
        strncat(cmd, " ",           sizeof(cmd) - strlen(cmd) - 1);
        strncat(cmd, tool->args[i], sizeof(cmd) - strlen(cmd) - 1);
    }
    strncat(cmd, "\r", sizeof(cmd) - strlen(cmd) - 1);

    pty_write(&fp->pty, cmd, (int)strlen(cmd));

    snprintf(tab->title, sizeof(tab->title), "%s", tool->name);
    printf("Launched tool: %s\n", tool->name);
}


/* ── Filtering helpers ──────────────────────────────────────── */

static int tool_matches(const ToolDef *tool, const char *search) {
    if (search[0] == '\0') return 1;

    /* Case-insensitive substring check on name */
    for (int i = 0; tool->name[i]; i++) {
        int match = 1;
        for (int j = 0; search[j]; j++) {
            char nc = tool->name[i + j];
            char sc = search[j];
            if (!nc) { match = 0; break; }
            if (nc >= 'A' && nc <= 'Z') nc += 32;
            if (sc >= 'A' && sc <= 'Z') sc += 32;
            if (nc != sc) { match = 0; break; }
        }
        if (match) return 1;
    }

    /* Case-insensitive substring check on description */
    for (int i = 0; tool->desc[i]; i++) {
        int match = 1;
        for (int j = 0; search[j]; j++) {
            char dc = tool->desc[i + j];
            char sc = search[j];
            if (!dc) { match = 0; break; }
            if (dc >= 'A' && dc <= 'Z') dc += 32;
            if (sc >= 'A' && sc <= 'Z') sc += 32;
            if (dc != sc) { match = 0; break; }
        }
        if (match) return 1;
    }

    return 0;
}

static int get_filtered(ToolManager *tm, const char *search,
                        int *out_indices, int max) {
    int count = 0;
    for (int i = 0; i < tm->count && count < max; i++) {
        if (tool_matches(&tm->tools[i], search))
            out_indices[count++] = i;
    }
    return count;
}


/* ── tools_launcher_handle_key ──────────────────────────────── */

int tools_launcher_handle_key(ToolManager *tm, SDL_Keycode sym,
                               SDL_Keymod mod, void *tabmgr_ptr,
                               int cols, int rows) {
    (void)mod;
    ToolLauncher *l = &tm->launcher;
    if (!l->visible) return 0;

    int indices[MAX_TOOLS];
    int count = get_filtered(tm, l->search, indices, MAX_TOOLS);

    switch (sym) {
        case SDLK_ESCAPE:
            tools_launcher_close(tm);
            return 1;

        case SDLK_RETURN:
            if (count > 0 && l->selected < count) {
                ToolDef *tool = &tm->tools[indices[l->selected]];
                tools_launcher_close(tm);
                tools_launch(tool, tabmgr_ptr, cols, rows);
            }
            return 1;

        case SDLK_UP:
            l->selected--;
            if (l->selected < 0)
                l->selected = (count > 0) ? count - 1 : 0;
            return 1;

        case SDLK_DOWN:
            l->selected++;
            if (l->selected >= count && count > 0)
                l->selected = 0;
            return 1;

        case SDLK_BACKSPACE:
            if (l->search_len > 0) {
                l->search[--l->search_len] = '\0';
                l->selected = 0;
            }
            return 1;

        default:
            return 0;
    }
}


/* ── tools_launcher_handle_text ─────────────────────────────── */

void tools_launcher_handle_text(ToolManager *tm, const char *text) {
    ToolLauncher *l = &tm->launcher;
    if (!l->visible) return;

    int len = (int)strlen(text);
    if (l->search_len + len < (int)sizeof(l->search) - 1) {
        strncat(l->search, text,
                sizeof(l->search) - (size_t)l->search_len - 1);
        l->search_len += len;
        l->selected = 0;
    }
}


/* ── draw_clipped_string ────────────────────────────────────── */

/*
 * Draw a string but stop before it overflows max_x pixels.
 * This prevents text from spilling outside the overlay box.
 *
 * We calculate how many characters fit:
 *   max_chars = (max_x - start_x) / cell_width
 * Then copy that many characters and draw.
 */
static void draw_clipped_string(Font *font, SDL_Renderer *renderer,
                                 const char *str,
                                 int x, int y,
                                 Uint8 r, Uint8 g, Uint8 b,
                                 int max_x) {
    if (x >= max_x) return;

    int max_chars = (max_x - x) / font->cell_width;
    if (max_chars <= 0) return;

    /* Copy up to max_chars characters into a local buffer */
    char buf[256];
    int  len = (int)strlen(str);
    if (len > max_chars) len = max_chars;
    if (len > 255) len = 255;

    memcpy(buf, str, (size_t)len);
    buf[len] = '\0';

    font_draw_string(font, renderer, buf, x, y, r, g, b);
}


/* ── tools_launcher_draw ────────────────────────────────────── */

/*
 * Renders the launcher overlay centered on the window.
 *
 * Fix for overflow: every text draw call now uses
 * draw_clipped_string() with max_x = right edge of the overlay
 * panel minus a small padding. No text can escape the box.
 *
 * Layout inside the panel:
 *   [  > search text...                    ]   ← search bar
 *   [------------------------------------------]  ← separator
 *   [  btop    Interactive system monitor  ]   ← tool row
 *   [  htop    Interactive process viewer  ]
 *   ...
 *   [  Enter=launch  Esc=close  Up/Down    ]   ← footer hint
 */
void tools_launcher_draw(ToolManager *tm, SDL_Renderer *renderer,
                         void *font_ptr, int win_w, int win_h) {
    ToolLauncher *l = &tm->launcher;
    if (!l->visible) return;

    Font *font = (Font *)font_ptr;

    /* Build filtered list */
    int indices[MAX_TOOLS];
    int count = get_filtered(tm, l->search, indices, MAX_TOOLS);

    /* Clamp selection */
    if (count > 0 && l->selected >= count) l->selected = count - 1;
    if (l->selected < 0) l->selected = 0;

    /* ── Overlay dimensions ── */
    int overlay_w  = 520;
    int row_h      = font->cell_height + 8;
    int rows_shown = (count < 8) ? count : 8;
    int search_h   = font->cell_height + 14;
    int footer_h   = font->cell_height + 8;
    int overlay_h  = search_h + 1
                     + (rows_shown > 0 ? rows_shown * row_h : row_h)
                     + footer_h + 8;

    /* Center horizontally, place at 1/3 from top */
    int ox = (win_w - overlay_w) / 2;
    int oy = (win_h - overlay_h) / 3;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;

    /* Right edge for clipping — leave 8px inner padding */
    int clip_right = ox + overlay_w - 8;

    /* ── Semi-transparent backdrop ── */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
    SDL_Rect backdrop = {0, 0, win_w, win_h};
    SDL_RenderFillRect(renderer, &backdrop);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    /* ── Panel background ── */
    SDL_SetRenderDrawColor(renderer, 28, 28, 34, 255);
    SDL_Rect panel = {ox, oy, overlay_w, overlay_h};
    SDL_RenderFillRect(renderer, &panel);

    /* ── Panel border ── */
    SDL_SetRenderDrawColor(renderer, 70, 130, 240, 255);
    SDL_RenderDrawRect(renderer, &panel);

    /* ── Search bar background ── */
    SDL_SetRenderDrawColor(renderer, 20, 20, 26, 255);
    SDL_Rect search_bg = {ox + 1, oy + 1,
                           overlay_w - 2, search_h};
    SDL_RenderFillRect(renderer, &search_bg);

    /* Search prompt ">" */
    int prompt_x = ox + 10;
    int text_y   = oy + (search_h - font->cell_height) / 2;
    font_draw_char(font, renderer, '>',
                   prompt_x, text_y,
                   70, 140, 255);

    /* Search text or placeholder */
    int search_text_x = prompt_x + font->cell_width + 6;
    if (l->search_len > 0) {
        draw_clipped_string(font, renderer, l->search,
                            search_text_x, text_y,
                            220, 220, 220, clip_right);
    } else {
        draw_clipped_string(font, renderer, "type to search...",
                            search_text_x, text_y,
                            70, 70, 80, clip_right);
    }

    /* Blinking cursor in search bar */
    Uint32 ticks = SDL_GetTicks();
    if ((ticks / 500) % 2 == 0) {
        int cur_x = search_text_x
                    + l->search_len * font->cell_width;
        if (cur_x < clip_right) {
            SDL_SetRenderDrawColor(renderer, 70, 140, 255, 255);
            SDL_Rect cur = {cur_x, text_y + 1,
                             2, font->cell_height - 2};
            SDL_RenderFillRect(renderer, &cur);
        }
    }

    /* ── Separator below search bar ── */
    int sep_y = oy + search_h;
    SDL_SetRenderDrawColor(renderer, 55, 55, 70, 255);
    SDL_RenderDrawLine(renderer, ox + 1, sep_y,
                       ox + overlay_w - 2, sep_y);

    /* ── Tool rows ── */
    if (count == 0) {
        /* No results message */
        int no_y = sep_y + 6;
        draw_clipped_string(font, renderer, "no tools match search",
                            ox + 12, no_y,
                            90, 90, 100, clip_right);
    } else {
        for (int i = 0; i < rows_shown; i++) {
            ToolDef *tool    = &tm->tools[indices[i]];
            int      row_y   = sep_y + 4 + i * row_h;
            int      is_sel  = (i == l->selected);

            /* Row highlight */
            if (is_sel) {
                SDL_SetRenderDrawColor(renderer,
                                       44, 74, 124, 255);
                SDL_Rect sel = {ox + 2, row_y,
                                 overlay_w - 4, row_h};
                SDL_RenderFillRect(renderer, &sel);
            }

            /*
             * Name column — fixed width of 10 characters.
             * max_x for name = ox + 12 + 10 * cell_width
             * This reserves the rest of the row for description.
             */
            int name_x    = ox + 12;
            int name_maxw = 10; /* characters reserved for name */
            int name_max_x = name_x + name_maxw * font->cell_width;

            Uint8 nr = is_sel ? 100 : 80;
            Uint8 ng = is_sel ? 200 : 160;
            Uint8 nb = is_sel ? 255 : 220;
            draw_clipped_string(font, renderer, tool->name,
                                name_x,
                                row_y + (row_h - font->cell_height) / 2,
                                nr, ng, nb, name_max_x);

            /*
             * Description column — starts after name column.
             * clip_right caps it at the right edge of the panel.
             */
            int desc_x = name_x + name_maxw * font->cell_width + 4;
            Uint8 dr = is_sel ? 190 : 130;
            Uint8 dg = is_sel ? 190 : 130;
            Uint8 db = is_sel ? 190 : 130;
            draw_clipped_string(font, renderer, tool->desc,
                                desc_x,
                                row_y + (row_h - font->cell_height) / 2,
                                dr, dg, db, clip_right);
        }
    }

    /* ── Footer hint ── */
    int footer_y = oy + overlay_h - footer_h + 2;

    /* Thin separator above footer */
    SDL_SetRenderDrawColor(renderer, 45, 45, 58, 255);
    SDL_RenderDrawLine(renderer,
                       ox + 1, footer_y - 2,
                       ox + overlay_w - 2, footer_y - 2);

    draw_clipped_string(font, renderer,
                        "Enter=launch  Esc=close  Up/Down=select",
                        ox + 8, footer_y,
                        60, 60, 75, clip_right);
}