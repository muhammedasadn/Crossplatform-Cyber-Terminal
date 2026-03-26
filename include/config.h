#ifndef CONFIG_H
#define CONFIG_H

/*
 * config.h — Runtime configuration for cterm.
 *
 * Reads ~/.config/cterm/cterm.conf at startup.
 * All settings have sensible defaults so the file is optional.
 *
 * Example cterm.conf:
 *   font_path  = /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf
 *   font_size  = 16
 *   win_width  = 1000
 *   win_height = 600
 *   fg_color   = 200,200,200
 *   bg_color   = 0,0,0
 *   shell      = /bin/bash
 *   scrollback = 5000
 *   tab_width  = 140
 */

#include <stdint.h>

/* Maximum path length */
#define CFG_PATH_MAX 512

typedef struct {
    /* Font */
    char     font_path[CFG_PATH_MAX];
    int      font_size;

    /* Window */
    int      win_width;
    int      win_height;

    /* Colors */
    uint8_t  fg_r, fg_g, fg_b;
    uint8_t  bg_r, bg_g, bg_b;

    /* Cursor blink interval in ms (0 = no blink) */
    int      cursor_blink_ms;

    /* Shell executable */
    char     shell[CFG_PATH_MAX];

    /* Scrollback lines */
    int      scrollback_lines;

    /* Tab bar */
    int      tab_width;
    int      tab_bar_height;
} Config;

/*
 * config_load — load config from ~/.config/cterm/cterm.conf.
 * If the file doesn't exist, writes a default config and uses it.
 * Always succeeds — missing or malformed lines use defaults.
 */
void config_load(Config *cfg);

/*
 * config_save_default — write a default config file.
 * Called automatically by config_load if no file exists.
 */
void config_save_default(const char *path, const Config *cfg);

#endif /* CONFIG_H */