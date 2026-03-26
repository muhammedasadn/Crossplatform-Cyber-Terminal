/*
 * tools.c — Built-in tool launcher implementation.
 *
 * Fix: launcher list now scrolls.
 *
 * Previously the list showed a fixed window of LAUNCHER_VISIBLE_ROWS
 * rows and had no scroll_offset — so tools below row 8 were
 * permanently hidden and unreachable.
 *
 * Now:
 *   scroll_offset tracks the first visible row in the filtered list.
 *   When the user presses Down and selected reaches the bottom of
 *   the visible window, scroll_offset advances to follow.
 *   When the user presses Up and selected goes above scroll_offset,
 *   scroll_offset retreats.
 *   PageUp/PageDown jump a full page at a time.
 *   A scroll indicator bar is drawn on the right edge of the list
 *   so the user can see where they are in a long list.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "tools.h"
#include "tabs.h"
#include "pane.h"
#include "font.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


/* ── Internal: register_tool ────────────────────────────────── */

static void register_tool(ToolManager *tm,
                           const char *name,
                           const char *desc,
                           const char *command,
                           int new_tab, ...) {
    if (tm->count >= MAX_TOOLS) return;

    ToolDef *t = &tm->tools[tm->count];
    memset(t, 0, sizeof(ToolDef));

    strncpy(t->name,    name,    sizeof(t->name)    - 1);
    strncpy(t->desc,    desc,    sizeof(t->desc)    - 1);
    strncpy(t->command, command, sizeof(t->command) - 1);
    t->new_tab  = new_tab;
    t->args[0]  = t->command;   /* argv[0] = program name */

    va_list vargs;
    va_start(vargs, new_tab);
    int idx = 1;
    while (idx < MAX_TOOL_ARGS - 1) {
        char *arg = va_arg(vargs, char *);
        if (!arg) break;
        t->args[idx++] = arg;
    }
    va_end(vargs);
    t->args[idx] = NULL;

    tm->count++;
}


/* ── tools_init ─────────────────────────────────────────────── */

void tools_init(ToolManager *tm) {
    memset(tm, 0, sizeof(ToolManager));

    /* System monitors */
    register_tool(tm, "btop",
        "Interactive system monitor (CPU RAM network disk)",
        "btop", 1, NULL);
    register_tool(tm, "htop",
        "Interactive process viewer and manager",
        "htop", 1, NULL);
    register_tool(tm, "top",
        "Classic Unix process monitor",
        "top", 1, NULL);
    register_tool(tm, "iotop",
        "Monitor disk I/O by process (needs root)",
        "iotop", 1, NULL);
    register_tool(tm, "iftop",
        "Monitor network bandwidth by connection",
        "iftop", 1, NULL);
    register_tool(tm, "nethogs",
        "Show network usage per process",
        "nethogs", 1, NULL);

    /* Text editors */
    register_tool(tm, "vim",
        "Vi IMproved — powerful modal text editor",
        "vim", 1, NULL);
    register_tool(tm, "nano",
        "Simple beginner-friendly terminal text editor",
        "nano", 1, NULL);
    register_tool(tm, "micro",
        "Modern terminal text editor with mouse support",
        "micro", 1, NULL);
    register_tool(tm, "emacs",
        "GNU Emacs text editor (terminal mode)",
        "emacs", 1, "-nw", NULL);

    /* Network tools */
    register_tool(tm, "nmap",
        "Network exploration and port scanner",
        "nmap", 1, "--help", NULL);
    register_tool(tm, "netstat",
        "Display network connections and routing",
        "netstat", 1, "-tulpn", NULL);
    register_tool(tm, "ss",
        "Socket statistics — modern netstat replacement",
        "ss", 1, "-tulpn", NULL);
    register_tool(tm, "curl",
        "Transfer data with URLs (HTTP FTP etc.)",
        "curl", 1, "--help", NULL);
    register_tool(tm, "wget",
        "Non-interactive network file downloader",
        "wget", 1, "--help", NULL);
    register_tool(tm, "dig",
        "DNS lookup utility",
        "dig", 1, NULL);
    register_tool(tm, "ping",
        "Send ICMP echo requests to a host",
        "ping", 1, "-c", "4", "8.8.8.8", NULL);
    register_tool(tm, "traceroute",
        "Trace network path to a host",
        "traceroute", 1, NULL);

    /* Security tools */
    register_tool(tm, "reaver",
        "WPS brute force attack tool (needs root + monitor)",
        "reaver", 1, "--help", NULL);
    register_tool(tm, "aircrack-ng",
        "WEP/WPA/WPA2 cracking suite",
        "aircrack-ng", 1, "--help", NULL);
    register_tool(tm, "john",
        "John the Ripper password cracker",
        "john", 1, "--help", NULL);

    /* File managers */
    register_tool(tm, "ranger",
        "Terminal file manager with vim keybindings",
        "ranger", 1, NULL);
    register_tool(tm, "mc",
        "Midnight Commander dual-pane file manager",
        "mc", 1, NULL);
    register_tool(tm, "ncdu",
        "NCurses disk usage — find large files fast",
        "ncdu", 1, NULL);

    /* Programming REPLs */
    register_tool(tm, "python3",
        "Python 3 interactive interpreter",
        "python3", 1, NULL);
    register_tool(tm, "node",
        "Node.js JavaScript runtime REPL",
        "node", 1, NULL);
    register_tool(tm, "lua",
        "Lua interactive interpreter",
        "lua", 1, NULL);
    register_tool(tm, "gdb",
        "GNU Debugger for C/C++ programs",
        "gdb", 1, NULL);

    /* Git */
    register_tool(tm, "lazygit",
        "Terminal UI for git",
        "lazygit", 1, NULL);
    register_tool(tm, "tig",
        "Text-mode git repository browser",
        "tig", 1, NULL);

    /* Shells */
    register_tool(tm, "bash",
        "New bash shell session",
        "/bin/bash", 1, NULL);
    register_tool(tm, "zsh",
        "Z shell with plugins support",
        "zsh", 1, NULL);
    register_tool(tm, "fish",
        "Friendly interactive shell",
        "fish", 1, NULL);

    printf("ToolManager: %d tools registered.\n", tm->count);
}


/* ── tools_launcher_open / close ────────────────────────────── */

void tools_launcher_open(ToolManager *tm) {
    ToolLauncher *l = &tm->launcher;
    l->visible       = 1;
    l->selected      = 0;
    l->scroll_offset = 0;    /* always start at top */
    l->search[0]     = '\0';
    l->search_len    = 0;
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

#ifdef CTERM_LINUX
    /* Give bash time to initialise before we type the command */
    usleep(150000);   /* 150 ms */

    Tab  *tab = tabs_get_active(tm);
    Pane *fp  = pane_get_focused(tab->root);
    if (!fp) return;

    /* Build: "command arg1 arg2\r" */
    char cmd[512] = {0};
    strncat(cmd, tool->command, sizeof(cmd) - 2);
    for (int i = 1; i < MAX_TOOL_ARGS && tool->args[i]; i++) {
        strncat(cmd, " ",          sizeof(cmd) - strlen(cmd) - 1);
        strncat(cmd, tool->args[i], sizeof(cmd) - strlen(cmd) - 1);
    }
    strncat(cmd, "\r", sizeof(cmd) - strlen(cmd) - 1);

    pty_write(&fp->pty, cmd, (int)strlen(cmd));

    /* Update tab title */
    tab = tabs_get_active(tm);
    snprintf(tab->title, sizeof(tab->title), "%s", tool->name);

    printf("tools_launch: '%s'\n", tool->name);
#else
    (void)tool;
#endif
}


/* ── Filtering ──────────────────────────────────────────────── */

/* Case-insensitive substring search */
static int istr_contains(const char *hay, const char *needle) {
    if (!needle || needle[0] == '\0') return 1;
    for (int i = 0; hay[i]; i++) {
        int ok = 1;
        for (int j = 0; needle[j] && ok; j++) {
            char h = hay[i+j], n = needle[j];
            if (h >= 'A' && h <= 'Z') h += 32;
            if (n >= 'A' && n <= 'Z') n += 32;
            if (h != n) ok = 0;
        }
        if (ok && needle[0]) return 1;
    }
    return 0;
}

static int tool_matches(const ToolDef *t, const char *s) {
    return istr_contains(t->name, s) || istr_contains(t->desc, s);
}

/* Fill out_indices[], return count of matches */
static int get_filtered(ToolManager *tm, const char *search,
                        int *out, int max) {
    int n = 0;
    for (int i = 0; i < tm->count && n < max; i++)
        if (tool_matches(&tm->tools[i], search))
            out[n++] = i;
    return n;
}


/* ── Scroll helpers ─────────────────────────────────────────── */

/*
 * ensure_selection_visible — adjusts scroll_offset so that
 * l->selected is always within the visible window.
 *
 * Called after every selection change.
 */
static void ensure_visible(ToolLauncher *l, int count) {
    if (count == 0) { l->scroll_offset = 0; return; }

    /* Clamp selected to valid range */
    if (l->selected < 0)      l->selected = 0;
    if (l->selected >= count) l->selected = count - 1;

    /* Scroll up if selected is above the visible window */
    if (l->selected < l->scroll_offset)
        l->scroll_offset = l->selected;

    /* Scroll down if selected is below the visible window */
    int bottom = l->scroll_offset + LAUNCHER_VISIBLE_ROWS - 1;
    if (l->selected > bottom)
        l->scroll_offset = l->selected - LAUNCHER_VISIBLE_ROWS + 1;

    /* Clamp scroll_offset */
    int max_offset = count - LAUNCHER_VISIBLE_ROWS;
    if (max_offset < 0)        max_offset = 0;
    if (l->scroll_offset < 0) l->scroll_offset = 0;
    if (l->scroll_offset > max_offset) l->scroll_offset = max_offset;
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
            ensure_visible(l, count);
            return 1;

        case SDLK_DOWN:
            l->selected++;
            ensure_visible(l, count);
            return 1;

        case SDLK_PAGEUP:
            /* Jump one full page up */
            l->selected      -= LAUNCHER_VISIBLE_ROWS;
            l->scroll_offset -= LAUNCHER_VISIBLE_ROWS;
            ensure_visible(l, count);
            return 1;

        case SDLK_PAGEDOWN:
            /* Jump one full page down */
            l->selected      += LAUNCHER_VISIBLE_ROWS;
            l->scroll_offset += LAUNCHER_VISIBLE_ROWS;
            ensure_visible(l, count);
            return 1;

        case SDLK_HOME:
            l->selected      = 0;
            l->scroll_offset = 0;
            return 1;

        case SDLK_END:
            l->selected = (count > 0) ? count - 1 : 0;
            ensure_visible(l, count);
            return 1;

        case SDLK_BACKSPACE:
            if (l->search_len > 0) {
                l->search[--l->search_len] = '\0';
                l->selected      = 0;
                l->scroll_offset = 0;
            }
            return 1;

        default:
            return 1;   /* consume all keys while open */
    }
}


/* ── tools_launcher_handle_text ─────────────────────────────── */

void tools_launcher_handle_text(ToolManager *tm, const char *text) {
    ToolLauncher *l = &tm->launcher;
    if (!l->visible) return;

    int len = (int)strlen(text);
    if (len <= 0) return;

    if (l->search_len + len < (int)sizeof(l->search) - 1) {
        strncat(l->search, text,
                sizeof(l->search) - (size_t)l->search_len - 1);
        l->search_len   += len;
        l->selected      = 0;   /* reset to top on new char */
        l->scroll_offset = 0;
    }
}


/* ── tools_launcher_draw ────────────────────────────────────── */

void tools_launcher_draw(ToolManager *tm, SDL_Renderer *renderer,
                         void *font_ptr, int win_w, int win_h) {
    ToolLauncher *l = &tm->launcher;
    if (!l->visible) return;

    Font *font = (Font *)font_ptr;

    /* Build filtered list */
    int indices[MAX_TOOLS];
    int count = get_filtered(tm, l->search, indices, MAX_TOOLS);

    /* Sync scroll after any filter change */
    ensure_visible(l, count);

    /* ── Layout ── */
    int overlay_w   = 580;
    int row_h       = font->cell_height + 8;
    int search_h    = font->cell_height + 14;
    int footer_h    = font->cell_height + 10;

    /* How many rows actually visible (min of LAUNCHER_VISIBLE_ROWS
       and how many results exist) */
    int visible     = count < LAUNCHER_VISIBLE_ROWS
                      ? count : LAUNCHER_VISIBLE_ROWS;
    if (visible < 1) visible = 1;   /* always show at least 1 row */

    int list_h      = visible * row_h;
    int overlay_h   = search_h + 4 + list_h + footer_h + 6;

    int ox = (win_w - overlay_w) / 2;
    int oy = (win_h - overlay_h) / 4;
    if (ox < 8) ox = 8;
    if (oy < 8) oy = 8;

    /* ── 1. Full-window backdrop ── */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 165);
    SDL_Rect backdrop = {0, 0, win_w, win_h};
    SDL_RenderFillRect(renderer, &backdrop);

    /* ── 2. Panel background ── */
    SDL_SetRenderDrawColor(renderer, 24, 24, 30, 255);
    SDL_Rect panel = {ox, oy, overlay_w, overlay_h};
    SDL_RenderFillRect(renderer, &panel);

    /* ── 3. Panel border ── */
    SDL_SetRenderDrawColor(renderer, 65, 125, 240, 255);
    SDL_RenderDrawRect(renderer, &panel);

    /* ── 4. Search bar ── */
    SDL_SetRenderDrawColor(renderer, 16, 16, 22, 255);
    SDL_Rect sbar = {ox+2, oy+2, overlay_w-4, search_h-2};
    SDL_RenderFillRect(renderer, &sbar);

    /* Prompt ">" */
    int py = oy + (search_h - font->cell_height) / 2;
    font_draw_char(font, renderer, '>', ox+10, py, 65, 135, 255);

    /* Search text or placeholder */
    int tx = ox + 10 + font->cell_width + 6;
    if (l->search_len > 0) {
        font_draw_string(font, renderer, l->search,
                         tx, py, 220, 220, 220);
    } else {
        font_draw_string(font, renderer, "type to search tools...",
                         tx, py, 60, 60, 72);
    }

    /* Blinking cursor in search bar */
    if ((SDL_GetTicks() / 530) % 2 == 0) {
        int cx = tx + l->search_len * font->cell_width;
        SDL_SetRenderDrawColor(renderer, 80, 160, 255, 200);
        SDL_Rect cur = {cx, py, 2, font->cell_height};
        SDL_RenderFillRect(renderer, &cur);
    }

    /* ── 5. Separator ── */
    int sep_y = oy + search_h;
    SDL_SetRenderDrawColor(renderer, 50, 60, 95, 255);
    SDL_RenderDrawLine(renderer, ox+1, sep_y, ox+overlay_w-2, sep_y);

    /* ── 6. Tool rows ── */
    int list_y = sep_y + 4;

    /* No results */
    if (count == 0) {
        font_draw_string(font, renderer,
                         "no tools match your search",
                         ox + overlay_w/2 - 13*font->cell_width,
                         list_y + 8, 80, 80, 95);
    }

    /*
     * Draw only the visible slice: rows [scroll_offset .. scroll_offset+visible)
     */
    for (int vi = 0; vi < visible; vi++) {
        int fi = l->scroll_offset + vi;   /* index in filtered list */
        if (fi >= count) break;

        int tool_idx = indices[fi];
        ToolDef *tool = &tm->tools[tool_idx];
        int row_y    = list_y + vi * row_h;
        int is_sel   = (fi == l->selected);

        /* Row highlight */
        if (is_sel) {
            SDL_SetRenderDrawColor(renderer, 38, 68, 128, 255);
            SDL_Rect sel_bg = {ox+2, row_y, overlay_w-4, row_h};
            SDL_RenderFillRect(renderer, &sel_bg);

            /* Left accent bar */
            SDL_SetRenderDrawColor(renderer, 80, 160, 255, 255);
            SDL_Rect accent = {ox+2, row_y, 3, row_h};
            SDL_RenderFillRect(renderer, &accent);
        }

        /* Row index number (helps user know position) */
        char num[8];
        snprintf(num, sizeof(num), "%2d", fi + 1);
        font_draw_string(font, renderer, num,
                         ox + 8, row_y + 4,
                         50, 60, 80);

        /* Tool name */
        Uint8 nr = is_sel ? 100 : 80;
        Uint8 ng = is_sel ? 200 : 160;
        Uint8 nb = is_sel ? 255 : 215;
        font_draw_string(font, renderer, tool->name,
                         ox + 8 + 3*font->cell_width, row_y + 4,
                         nr, ng, nb);

        /* Description — offset past fixed name column (12 chars) */
        int desc_x = ox + 8 + 3*font->cell_width
                     + 12*font->cell_width;

        /* Available width for description */
        int avail_chars = (overlay_w - (desc_x - ox) - 20)
                          / font->cell_width;
        if (avail_chars < 1) avail_chars = 1;

        char desc[128] = {0};
        strncpy(desc, tool->desc,
                (size_t)avail_chars < sizeof(desc)-1
                ? (size_t)avail_chars : sizeof(desc)-1);
        if ((int)strlen(tool->desc) > avail_chars && avail_chars > 3) {
            desc[avail_chars-3] = '.';
            desc[avail_chars-2] = '.';
            desc[avail_chars-1] = '.';
            desc[avail_chars]   = '\0';
        }

        Uint8 dr = is_sel ? 165 : 105;
        Uint8 dg = is_sel ? 165 : 105;
        Uint8 db = is_sel ? 165 : 105;
        font_draw_string(font, renderer, desc,
                         desc_x, row_y + 4, dr, dg, db);

        /* Subtle row divider */
        if (vi < visible - 1 && !is_sel) {
            SDL_SetRenderDrawColor(renderer, 35, 35, 45, 255);
            SDL_RenderDrawLine(renderer,
                               ox+8, row_y+row_h-1,
                               ox+overlay_w-8, row_y+row_h-1);
        }
    }

    /* ── 7. Scroll indicator bar ── */
    /*
     * Drawn on the right edge of the list area.
     * Shows proportional position in the full filtered list.
     * Only drawn when there are more items than visible rows.
     */
    if (count > LAUNCHER_VISIBLE_ROWS) {
        int bar_track_h = list_h;
        int bar_track_y = list_y;

        /* Track background */
        SDL_SetRenderDrawColor(renderer, 30, 30, 40, 255);
        SDL_Rect track = {ox+overlay_w-8, bar_track_y, 6, bar_track_h};
        SDL_RenderFillRect(renderer, &track);

        /* Thumb size proportional to visible/total ratio */
        float ratio    = (float)LAUNCHER_VISIBLE_ROWS / (float)count;
        int   thumb_h  = (int)(bar_track_h * ratio);
        if (thumb_h < 12) thumb_h = 12;

        /* Thumb position proportional to scroll_offset */
        float pos_ratio = (float)l->scroll_offset
                          / (float)(count - LAUNCHER_VISIBLE_ROWS);
        int   thumb_y   = bar_track_y
                          + (int)((bar_track_h - thumb_h) * pos_ratio);

        SDL_SetRenderDrawColor(renderer, 70, 130, 240, 200);
        SDL_Rect thumb = {ox+overlay_w-8, thumb_y, 6, thumb_h};
        SDL_RenderFillRect(renderer, &thumb);
    }

    /* ── 8. Footer separator ── */
    int fsep_y = list_y + list_h + 2;
    SDL_SetRenderDrawColor(renderer, 42, 48, 72, 255);
    SDL_RenderDrawLine(renderer, ox+1, fsep_y, ox+overlay_w-2, fsep_y);

    /* ── 9. Footer hints ── */
    int fy = fsep_y + (footer_h - font->cell_height) / 2;

    font_draw_string(font, renderer, "Enter=launch",
                     ox+10, fy, 55, 110, 190);
    font_draw_string(font, renderer, "Esc=close",
                     ox+10+13*font->cell_width, fy, 55, 110, 190);
    font_draw_string(font, renderer, "PgUp/Dn=page",
                     ox+10+23*font->cell_width, fy, 55, 110, 190);

    /* Result count — right aligned */
    char cstr[32];
    snprintf(cstr, sizeof(cstr), "%d/%d tools",
             count, tm->count);
    int cw = (int)strlen(cstr) * font->cell_width;
    font_draw_string(font, renderer, cstr,
                     ox+overlay_w-cw-10, fy, 50, 60, 80);
}