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

static void set_color(SDL_Renderer *renderer, int r, int g, int b, int a) {
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
}

static int tool_installed(const char *cmd) {
    if (cmd[0] == '/') {
        return access(cmd, X_OK) == 0;
    }

    const char *path_env = getenv("PATH");
    if (!path_env) {
        path_env = "/usr/bin:/bin:/usr/local/bin";
    }

    char path_copy[4096];
    strncpy(path_copy, path_env, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char *dir = strtok(path_copy, ":");
    while (dir) {
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", dir, cmd);
        if (access(full, X_OK) == 0) {
            return 1;
        }
        dir = strtok(NULL, ":");
    }
    return 0;
}

static void register_tool(ToolManager *tm, const char *name,
                          const char *desc, const char *cmd,
                          int new_tab, ...) {
    if (tm->count >= MAX_TOOLS || !tool_installed(cmd)) {
        return;
    }

    ToolDef *tool = &tm->tools[tm->count];
    memset(tool, 0, sizeof(ToolDef));
    strncpy(tool->name, name, sizeof(tool->name) - 1);
    strncpy(tool->desc, desc, sizeof(tool->desc) - 1);
    strncpy(tool->command, cmd, sizeof(tool->command) - 1);
    tool->new_tab = new_tab;
    tool->args[0] = tool->command;

    va_list args;
    va_start(args, new_tab);
    int idx = 1;
    while (idx < MAX_TOOL_ARGS - 1) {
        char *arg = va_arg(args, char *);
        if (!arg) {
            break;
        }
        tool->args[idx++] = arg;
    }
    va_end(args);

    tool->args[idx] = NULL;
    tm->count++;
}

void tools_init(ToolManager *tm) {
    memset(tm, 0, sizeof(ToolManager));

    register_tool(tm, "btop", "System monitor", "btop", 1, NULL);
    register_tool(tm, "htop", "Interactive process viewer", "htop", 1, NULL);
    register_tool(tm, "top", "Classic process monitor", "top", 1, NULL);
    register_tool(tm, "iotop", "Disk I/O monitor", "iotop", 1, NULL);
    register_tool(tm, "iftop", "Network bandwidth monitor", "iftop", 1, NULL);
    register_tool(tm, "glances", "Cross-platform system monitor", "glances", 1, NULL);

    register_tool(tm, "vim", "Vi IMproved text editor", "vim", 1, NULL);
    register_tool(tm, "nvim", "Neovim text editor", "nvim", 1, NULL);
    register_tool(tm, "nano", "Simple terminal editor", "nano", 1, NULL);
    register_tool(tm, "micro", "Modern terminal editor", "micro", 1, NULL);
    register_tool(tm, "emacs", "GNU Emacs in terminal mode", "emacs", 1, "-nw", NULL);

    register_tool(tm, "nmap", "Network port scanner", "nmap", 1, "--help", NULL);
    register_tool(tm, "netstat", "Network connections display", "netstat", 1, "-tulpn", NULL);
    register_tool(tm, "ss", "Socket statistics", "ss", 1, "-tulpn", NULL);
    register_tool(tm, "curl", "HTTP and FTP transfer tool", "curl", 1, "--help", NULL);
    register_tool(tm, "wget", "Network file downloader", "wget", 1, "--help", NULL);
    register_tool(tm, "dig", "DNS lookup utility", "dig", 1, NULL);
    register_tool(tm, "mtr", "Network diagnostic tool", "mtr", 1, NULL);
    register_tool(tm, "tcpdump", "Network packet analyzer", "tcpdump", 1, "--help", NULL);

    register_tool(tm, "reaver", "WPS brute force tool", "reaver", 1, "--help", NULL);
    register_tool(tm, "aircrack-ng", "WPA and WEP cracking suite", "aircrack-ng", 1, "--help", NULL);
    register_tool(tm, "john", "Password cracker", "john", 1, "--help", NULL);
    register_tool(tm, "hashcat", "GPU password cracker", "hashcat", 1, "--help", NULL);
    register_tool(tm, "sqlmap", "SQL injection scanner", "sqlmap", 1, "--help", NULL);
    register_tool(tm, "hydra", "Login brute forcer", "hydra", 1, "--help", NULL);
    register_tool(tm, "nikto", "Web server scanner", "nikto", 1, "--help", NULL);

    register_tool(tm, "ranger", "Vim-style file manager", "ranger", 1, NULL);
    register_tool(tm, "mc", "Midnight Commander", "mc", 1, NULL);
    register_tool(tm, "ncdu", "Disk usage analyzer", "ncdu", 1, NULL);
    register_tool(tm, "nnn", "Fast file manager", "nnn", 1, NULL);

    register_tool(tm, "python3", "Python 3 REPL", "python3", 1, NULL);
    register_tool(tm, "node", "Node.js REPL", "node", 1, NULL);
    register_tool(tm, "lua", "Lua interpreter", "lua", 1, NULL);
    register_tool(tm, "gdb", "GNU debugger", "gdb", 1, NULL);
    register_tool(tm, "cargo", "Rust package manager", "cargo", 1, "--help", NULL);
    register_tool(tm, "lazygit", "Terminal Git UI", "lazygit", 1, NULL);
    register_tool(tm, "tig", "Text-mode Git browser", "tig", 1, NULL);
    register_tool(tm, "mysql", "MySQL client", "mysql", 1, "--help", NULL);
    register_tool(tm, "psql", "PostgreSQL client", "psql", 1, "--help", NULL);
    register_tool(tm, "sqlite3", "SQLite client", "sqlite3", 1, NULL);

    register_tool(tm, "bash", "New Bash shell", "/bin/bash", 1, NULL);
    register_tool(tm, "zsh", "Z shell", "zsh", 1, NULL);
    register_tool(tm, "fish", "Fish shell", "fish", 1, NULL);
}

void tools_launcher_open(ToolManager *tm) {
    ToolLauncher *launcher = &tm->launcher;
    launcher->visible = 1;
    launcher->selected = 0;
    launcher->scroll_offset = 0;
    launcher->search[0] = '\0';
    launcher->search_len = 0;
}

void tools_launcher_close(ToolManager *tm) {
    tm->launcher.visible = 0;
}

void tools_launch(ToolDef *tool, void *tabmgr_ptr, int cols, int rows) {
    TabManager *tm = (TabManager *)tabmgr_ptr;
    if (tabs_new(tm, cols, rows) != 0) {
        return;
    }

#ifdef CTERM_LINUX
    usleep(140000);
    Tab *tab = tabs_get_active(tm);
    Pane *pane = pane_get_focused(tab->root);
    if (!pane) {
        return;
    }

    char cmd[512] = {0};
    strncat(cmd, tool->command, sizeof(cmd) - 2);
    for (int i = 1; i < MAX_TOOL_ARGS && tool->args[i]; i++) {
        strncat(cmd, " ", sizeof(cmd) - strlen(cmd) - 1);
        strncat(cmd, tool->args[i], sizeof(cmd) - strlen(cmd) - 1);
    }
    strncat(cmd, "\r", sizeof(cmd) - strlen(cmd) - 1);
    pty_write(&pane->pty, cmd, (int)strlen(cmd));

    tab = tabs_get_active(tm);
    snprintf(tab->title, sizeof(tab->title), "%s", tool->name);
#endif
}

static int contains_ci(const char *haystack, const char *needle) {
    if (!needle || !needle[0]) {
        return 1;
    }

    for (int i = 0; haystack[i]; i++) {
        int ok = 1;
        for (int j = 0; needle[j] && ok; j++) {
            char h = haystack[i + j];
            char n = needle[j];
            if (h >= 'A' && h <= 'Z') {
                h += 32;
            }
            if (n >= 'A' && n <= 'Z') {
                n += 32;
            }
            if (h != n) {
                ok = 0;
            }
        }
        if (ok) {
            return 1;
        }
    }
    return 0;
}

static int get_filtered(ToolManager *tm, const char *search,
                        int *out, int max) {
    int count = 0;
    for (int i = 0; i < tm->count && count < max; i++) {
        if (contains_ci(tm->tools[i].name, search) ||
            contains_ci(tm->tools[i].desc, search)) {
            out[count++] = i;
        }
    }
    return count;
}

static void copy_clamped_label(char *dst, size_t dst_size,
                               const char *src, int max_chars) {
    if (dst_size == 0) {
        return;
    }

    dst[0] = '\0';
    if (!src || max_chars <= 0) {
        return;
    }

    size_t limit = (size_t)max_chars;
    if (limit >= dst_size) {
        limit = dst_size - 1;
    }

    size_t src_len = strlen(src);
    size_t copy_len = src_len < limit ? src_len : limit;
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';

    if (src_len > copy_len && copy_len >= 3) {
        dst[copy_len - 3] = '.';
        dst[copy_len - 2] = '.';
        dst[copy_len - 1] = '.';
    }
}

static void ensure_visible(ToolLauncher *launcher, int count) {
    if (count == 0) {
        launcher->scroll_offset = 0;
        return;
    }

    if (launcher->selected < 0) {
        launcher->selected = 0;
    }
    if (launcher->selected >= count) {
        launcher->selected = count - 1;
    }
    if (launcher->selected < launcher->scroll_offset) {
        launcher->scroll_offset = launcher->selected;
    }

    int bottom = launcher->scroll_offset + LAUNCHER_ROWS - 1;
    if (launcher->selected > bottom) {
        launcher->scroll_offset = launcher->selected - LAUNCHER_ROWS + 1;
    }

    int max_offset = count - LAUNCHER_ROWS;
    if (max_offset < 0) {
        max_offset = 0;
    }
    if (launcher->scroll_offset < 0) {
        launcher->scroll_offset = 0;
    }
    if (launcher->scroll_offset > max_offset) {
        launcher->scroll_offset = max_offset;
    }
}

int tools_launcher_handle_key(ToolManager *tm, SDL_Keycode sym,
                              SDL_Keymod mod, void *tabmgr_ptr,
                              int cols, int rows) {
    (void)mod;

    ToolLauncher *launcher = &tm->launcher;
    if (!launcher->visible) {
        return 0;
    }

    int indices[MAX_TOOLS];
    int count = get_filtered(tm, launcher->search, indices, MAX_TOOLS);

    switch (sym) {
        case SDLK_ESCAPE:
            tools_launcher_close(tm);
            return 1;
        case SDLK_RETURN:
            if (count > 0 && launcher->selected < count) {
                ToolDef *tool = &tm->tools[indices[launcher->selected]];
                tools_launcher_close(tm);
                tools_launch(tool, tabmgr_ptr, cols, rows);
            }
            return 1;
        case SDLK_UP:
            launcher->selected--;
            ensure_visible(launcher, count);
            return 1;
        case SDLK_DOWN:
            launcher->selected++;
            ensure_visible(launcher, count);
            return 1;
        case SDLK_PAGEUP:
            launcher->selected -= LAUNCHER_ROWS;
            launcher->scroll_offset -= LAUNCHER_ROWS;
            ensure_visible(launcher, count);
            return 1;
        case SDLK_PAGEDOWN:
            launcher->selected += LAUNCHER_ROWS;
            launcher->scroll_offset += LAUNCHER_ROWS;
            ensure_visible(launcher, count);
            return 1;
        case SDLK_HOME:
            launcher->selected = 0;
            launcher->scroll_offset = 0;
            return 1;
        case SDLK_END:
            launcher->selected = count > 0 ? count - 1 : 0;
            ensure_visible(launcher, count);
            return 1;
        case SDLK_BACKSPACE:
            if (launcher->search_len > 0) {
                launcher->search[--launcher->search_len] = '\0';
                launcher->selected = 0;
                launcher->scroll_offset = 0;
            }
            return 1;
        default:
            return 1;
    }
}

void tools_launcher_handle_text(ToolManager *tm, const char *text) {
    ToolLauncher *launcher = &tm->launcher;
    if (!launcher->visible) {
        return;
    }

    int len = (int)strlen(text);
    if (len <= 0) {
        return;
    }

    if (launcher->search_len + len < (int)sizeof(launcher->search) - 1) {
        strncat(launcher->search, text,
                sizeof(launcher->search) - (size_t)launcher->search_len - 1);
        launcher->search_len += len;
        launcher->selected = 0;
        launcher->scroll_offset = 0;
    }
}

void tools_launcher_draw(ToolManager *tm, SDL_Renderer *renderer,
                         void *font_ptr, int win_w, int win_h) {
    ToolLauncher *launcher = &tm->launcher;
    if (!launcher->visible) {
        return;
    }

    Font *font = (Font *)font_ptr;
    int indices[MAX_TOOLS];
    int count = get_filtered(tm, launcher->search, indices, MAX_TOOLS);
    ensure_visible(launcher, count);

    int cw = font->cell_width > 0 ? font->cell_width : 8;
    int ch = font->cell_height > 0 ? font->cell_height : 16;

    int header_h = ch + 8;
    int row_h = ch + 6;
    int visible = count < LAUNCHER_ROWS ? count : LAUNCHER_ROWS;
    if (visible < 2) {
        visible = 2;
    }
    int list_h = visible * row_h + 4;
    int panel_h = ch * 3 + 20;
    int footer_h = ch + 8;
    int overlay_h = header_h + 1 + list_h + 1 + panel_h + 1 + footer_h;

    int overlay_w = (win_w * 70) / 100;
    if (overlay_w > 680) {
        overlay_w = 680;
    }
    if (overlay_w < 380) {
        overlay_w = 380;
    }

    int ox = (win_w - overlay_w) / 2;
    int oy = (win_h - overlay_h) / 4;
    if (oy < 20) {
        oy = 20;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    set_color(renderer, 0, 0, 0, 180);
    SDL_Rect backdrop = {0, 0, win_w, win_h};
    SDL_RenderFillRect(renderer, &backdrop);

    set_color(renderer, RT_BORDER_R, RT_BORDER_G, RT_BORDER_B, 255);
    SDL_Rect border_outer = {ox - 2, oy - 2, overlay_w + 4, overlay_h + 4};
    SDL_RenderDrawRect(renderer, &border_outer);
    set_color(renderer, RT_DIM_R, RT_DIM_G, RT_DIM_B, 180);
    SDL_Rect border_inner = {ox - 1, oy - 1, overlay_w + 2, overlay_h + 2};
    SDL_RenderDrawRect(renderer, &border_inner);

    set_color(renderer, RT_BG_R, RT_BG_G, RT_BG_B, 255);
    SDL_Rect panel = {ox, oy, overlay_w, overlay_h};
    SDL_RenderFillRect(renderer, &panel);

    int y = oy;

    set_color(renderer, RT_HEADER_R + 4, RT_HEADER_G + 8, RT_HEADER_B + 4, 255);
    SDL_Rect header = {ox, y, overlay_w, header_h};
    SDL_RenderFillRect(renderer, &header);

    font_draw_string(font, renderer, "SELECT TOOL",
                     ox + 10, y + (header_h - ch) / 2,
                     RT_ACCENT_R, RT_ACCENT_G, RT_ACCENT_B);

    char header_count[32];
    snprintf(header_count, sizeof(header_count), "%d / %d", count, tm->count);
    int header_x = ox + overlay_w - (int)strlen(header_count) * cw - 10;
    font_draw_string(font, renderer, header_count,
                     header_x, y + (header_h - ch) / 2,
                     RT_DIM_R + 20, RT_DIM_G + 20, RT_DIM_B);

    y += header_h;
    set_color(renderer, RT_BORDER_R, RT_BORDER_G, RT_BORDER_B, 255);
    SDL_RenderDrawLine(renderer, ox, y, ox + overlay_w, y);
    y++;

    int list_top = y;
    y += 2;

    font_draw_string(font, renderer, "Name",
                     ox + 10, y, RT_DIM_R, RT_DIM_G, RT_DIM_B);
    font_draw_string(font, renderer, "Description",
                     ox + 14 * cw, y, RT_DIM_R, RT_DIM_G, RT_DIM_B);
    y += ch + 2;

    set_color(renderer, RT_DIM_R, RT_DIM_G, RT_DIM_B, 120);
    SDL_RenderDrawLine(renderer, ox + 4, y, ox + overlay_w - 4, y);
    y += 2;

    if (count == 0) {
        font_draw_string(font, renderer, "No tools match",
                         ox + overlay_w / 2 - 7 * cw, y + 4,
                         RT_DIM_R, RT_DIM_G, RT_DIM_B);
    }

    for (int vi = 0; vi < visible; vi++) {
        int fi = launcher->scroll_offset + vi;
        if (fi >= count) {
            break;
        }

        ToolDef *tool = &tm->tools[indices[fi]];
        int row_y = y + vi * row_h;
        int is_selected = (fi == launcher->selected);

        if (is_selected) {
            set_color(renderer, RT_SEL_BG_R, RT_SEL_BG_G, RT_SEL_BG_B, 255);
            SDL_Rect selected_bg = {ox + 1, row_y, overlay_w - 2, row_h - 1};
            SDL_RenderFillRect(renderer, &selected_bg);
        } else if (vi % 2 == 0) {
            set_color(renderer, RT_BG_R + 2, RT_BG_G + 4, RT_BG_B + 2, 255);
            SDL_Rect alt_bg = {ox + 1, row_y, overlay_w - 2, row_h - 1};
            SDL_RenderFillRect(renderer, &alt_bg);
        }

        Uint8 name_r = is_selected ? RT_SEL_FG_R : RT_TEXT_R;
        Uint8 name_g = is_selected ? RT_SEL_FG_G : RT_TEXT_G;
        Uint8 name_b = is_selected ? RT_SEL_FG_B : RT_TEXT_B;
        font_draw_string(font, renderer, tool->name,
                         ox + 10, row_y + (row_h - ch) / 2,
                         name_r, name_g, name_b);

        int desc_x = ox + 14 * cw;
        int available = (overlay_w - (desc_x - ox) - 14) / cw;
        if (available > 0) {
            char desc[80] = {0};
            copy_clamped_label(desc, sizeof(desc), tool->desc, available);

            Uint8 desc_r = is_selected ? RT_SEL_FG_R : RT_DIM_R + 30;
            Uint8 desc_g = is_selected ? RT_SEL_FG_G : RT_DIM_G + 30;
            Uint8 desc_b = is_selected ? RT_SEL_FG_B : RT_DIM_B;
            font_draw_string(font, renderer, desc,
                             desc_x, row_y + (row_h - ch) / 2,
                             desc_r, desc_g, desc_b);
        }

        if (!is_selected && vi < visible - 1) {
            set_color(renderer, RT_BG_R + 8, RT_BG_G + 14, RT_BG_B + 8, 255);
            SDL_RenderDrawLine(renderer,
                               ox + 4, row_y + row_h - 1,
                               ox + overlay_w - 4, row_y + row_h - 1);
        }
    }

    y = list_top + list_h;

    if (count > LAUNCHER_ROWS) {
        int track_y = list_top + ch + 6;
        int track_h = visible * row_h;
        set_color(renderer, RT_DIM_R, RT_DIM_G, RT_DIM_B, 80);
        SDL_Rect track = {ox + overlay_w - 5, track_y, 3, track_h};
        SDL_RenderFillRect(renderer, &track);

        float ratio = (float)LAUNCHER_ROWS / (float)count;
        int thumb_h = (int)(track_h * ratio);
        if (thumb_h < 8) {
            thumb_h = 8;
        }
        float progress = (float)launcher->scroll_offset
                       / (float)(count - LAUNCHER_ROWS);
        int thumb_y = track_y + (int)((track_h - thumb_h) * progress);

        set_color(renderer, RT_SEL_BG_R, RT_SEL_BG_G, RT_SEL_BG_B, 200);
        SDL_Rect thumb = {ox + overlay_w - 5, thumb_y, 3, thumb_h};
        SDL_RenderFillRect(renderer, &thumb);
    }

    set_color(renderer, RT_BORDER_R, RT_BORDER_G, RT_BORDER_B, 255);
    SDL_RenderDrawLine(renderer, ox, y, ox + overlay_w, y);
    y++;

    set_color(renderer, RT_PANEL_R, RT_PANEL_G, RT_PANEL_B, 255);
    SDL_Rect status = {ox, y, overlay_w, panel_h};
    SDL_RenderFillRect(renderer, &status);

    int py = y + 6;
    int label_x = ox + 10;
    int value_x = ox + 10 * cw;

    if (count > 0 && launcher->selected < count) {
        ToolDef *selected = &tm->tools[indices[launcher->selected]];

        font_draw_string(font, renderer, "Name",
                         label_x, py, RT_DIM_R + 20, RT_DIM_G + 20, RT_DIM_B);
        font_draw_string(font, renderer, selected->name,
                         value_x, py, RT_TEXT_R, RT_TEXT_G, RT_TEXT_B);
        py += ch + 4;

        font_draw_string(font, renderer, "Desc",
                         label_x, py, RT_DIM_R + 20, RT_DIM_G + 20, RT_DIM_B);
        int short_available = (overlay_w - (value_x - ox) - 14) / cw;
        char short_desc[64] = {0};
        copy_clamped_label(short_desc, sizeof(short_desc),
                           selected->desc, short_available);
        font_draw_string(font, renderer, short_desc,
                         value_x, py, RT_DIM_R + 50, RT_DIM_G + 50, RT_DIM_B);
        py += ch + 6;
    } else {
        py += (ch + 4) * 2 + 6;
    }

    font_draw_string(font, renderer, "Search",
                     label_x, py, RT_DIM_R + 20, RT_DIM_G + 20, RT_DIM_B);

    int box_x = value_x - 4;
    int box_w = overlay_w - (value_x - ox) - 14;
    int box_h = ch + 6;
    set_color(renderer, RT_INPUT_BG_R, RT_INPUT_BG_G, RT_INPUT_BG_B, 255);
    SDL_Rect input = {box_x, py - 2, box_w, box_h};
    SDL_RenderFillRect(renderer, &input);
    set_color(renderer, RT_BORDER_R, RT_BORDER_G, RT_BORDER_B, 255);
    SDL_RenderDrawRect(renderer, &input);

    if (launcher->search_len > 0) {
        font_draw_string(font, renderer, launcher->search,
                         box_x + 4, py, RT_TEXT_R, RT_TEXT_G, RT_TEXT_B);
    } else {
        font_draw_string(font, renderer, "type to filter...",
                         box_x + 4, py, RT_DIM_R, RT_DIM_G, RT_DIM_B);
    }

    if ((SDL_GetTicks() / 530) % 2 == 0) {
        int cursor_x = box_x + 4 + launcher->search_len * cw;
        set_color(renderer, RT_CURSOR_R, RT_CURSOR_G, RT_CURSOR_B, 220);
        SDL_Rect cursor = {cursor_x, py, 2, ch};
        SDL_RenderFillRect(renderer, &cursor);
    }

    y += panel_h;
    set_color(renderer, RT_BORDER_R, RT_BORDER_G, RT_BORDER_B, 255);
    SDL_RenderDrawLine(renderer, ox, y, ox + overlay_w, y);
    y++;

    set_color(renderer, RT_HEADER_R + 4, RT_HEADER_G + 8, RT_HEADER_B + 4, 255);
    SDL_Rect footer = {ox, y, overlay_w, footer_h};
    SDL_RenderFillRect(renderer, &footer);

    int footer_y = y + (footer_h - ch) / 2;
    font_draw_string(font, renderer, "Enter=Launch",
                     ox + 10, footer_y, RT_DIM_R + 30, RT_DIM_G + 30, RT_DIM_B);
    font_draw_string(font, renderer, "Esc=Close",
                     ox + 10 + 13 * cw, footer_y,
                     RT_DIM_R + 30, RT_DIM_G + 30, RT_DIM_B);
    font_draw_string(font, renderer, "PgDn=Scroll",
                     ox + 10 + 23 * cw, footer_y,
                     RT_DIM_R + 30, RT_DIM_G + 30, RT_DIM_B);

    char count_text[24];
    snprintf(count_text, sizeof(count_text), "%d tools", count);
    int count_x = ox + overlay_w - (int)strlen(count_text) * cw - 10;
    font_draw_string(font, renderer, count_text, count_x, footer_y,
                     RT_DIM_R, RT_DIM_G, RT_DIM_B);

    set_color(renderer, RT_BORDER_R, RT_BORDER_G, RT_BORDER_B, 255);
    SDL_RenderDrawLine(renderer, ox, oy + overlay_h - 1,
                       ox + overlay_w, oy + overlay_h - 1);
}
