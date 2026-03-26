/*
 * config.c — Configuration file loader for cterm.
 *
 * Format: key = value (one per line)
 * Lines starting with # are comments.
 * Unknown keys are silently ignored.
 * Malformed values fall back to the compiled-in default.
 *
 * Config file location: ~/.config/cterm/cterm.conf
 * If it doesn't exist, we create it with default values
 * so the user has a template to edit.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <ctype.h>


/* ── Compiled-in defaults ───────────────────────────────────── */

static void set_defaults(Config *cfg) {
    /* Font — try a common monospace font path */
    strncpy(cfg->font_path,
            "../assets/font.ttf",
            CFG_PATH_MAX - 1);
    cfg->font_size        = 16;

    /* Window */
    cfg->win_width        = 900;
    cfg->win_height       = 550;

    /* Colors: light gray text on black background */
    cfg->fg_r = 200; cfg->fg_g = 200; cfg->fg_b = 200;
    cfg->bg_r = 0;   cfg->bg_g = 0;   cfg->bg_b = 0;

    /* Cursor blinks every 500ms */
    cfg->cursor_blink_ms  = 500;

    /* Shell */
    strncpy(cfg->shell, "/bin/bash", CFG_PATH_MAX - 1);

    /* Scrollback */
    cfg->scrollback_lines = 5000;

    /* Tab bar */
    cfg->tab_width        = 140;
    cfg->tab_bar_height   = 30;
}


/* ── String helpers ─────────────────────────────────────────── */

/* Trim leading and trailing whitespace in-place */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}


/* ── config_save_default ────────────────────────────────────── */

void config_save_default(const char *path, const Config *cfg) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "config: cannot write default to %s: %s\n",
                path, strerror(errno));
        return;
    }

    fprintf(f,
        "# cterm configuration file\n"
        "# Lines starting with # are comments.\n"
        "# Restart cterm after editing.\n"
        "\n"
        "# Font settings\n"
        "font_path  = %s\n"
        "font_size  = %d\n"
        "\n"
        "# Window size (pixels)\n"
        "win_width  = %d\n"
        "win_height = %d\n"
        "\n"
        "# Default foreground color (r,g,b  range 0-255)\n"
        "fg_color   = %d,%d,%d\n"
        "\n"
        "# Default background color (r,g,b  range 0-255)\n"
        "bg_color   = %d,%d,%d\n"
        "\n"
        "# Cursor blink interval in milliseconds (0 = no blink)\n"
        "cursor_blink_ms = %d\n"
        "\n"
        "# Shell executable\n"
        "shell      = %s\n"
        "\n"
        "# Scrollback buffer size (lines)\n"
        "scrollback = %d\n"
        "\n"
        "# Tab bar settings\n"
        "tab_width       = %d\n"
        "tab_bar_height  = %d\n",
        cfg->font_path, cfg->font_size,
        cfg->win_width, cfg->win_height,
        cfg->fg_r, cfg->fg_g, cfg->fg_b,
        cfg->bg_r, cfg->bg_g, cfg->bg_b,
        cfg->cursor_blink_ms,
        cfg->shell,
        cfg->scrollback_lines,
        cfg->tab_width,
        cfg->tab_bar_height
    );

    fclose(f);
    printf("config: wrote default config to %s\n", path);
}


/* ── config_load ────────────────────────────────────────────── */

void config_load(Config *cfg) {
    /* Start with compiled-in defaults */
    set_defaults(cfg);

    /* Build config directory path: ~/.config/cterm/ */
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        fprintf(stderr, "config: $HOME not set, using defaults\n");
        return;
    }

    char dir_path[CFG_PATH_MAX];
    snprintf(dir_path, sizeof(dir_path),
             "%s/.config/cterm", home);

    char file_path[CFG_PATH_MAX];
    snprintf(file_path, sizeof(file_path),
             "%s/cterm.conf", dir_path);

    /* Create directory if it doesn't exist */
    struct stat st;
    if (stat(dir_path, &st) != 0) {
        /*
         * mkdir with 0755 = rwxr-xr-x permissions.
         * Only the owner can write; others can read and execute.
         */
        if (mkdir(dir_path, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "config: cannot create %s: %s\n",
                    dir_path, strerror(errno));
            return;
        }
    }

    /* If no config file exists, write a default and use it */
    FILE *f = fopen(file_path, "r");
    if (!f) {
        config_save_default(file_path, cfg);
        return;
    }

    printf("config: loading %s\n", file_path);

    /* Parse each line */
    char line[512];
    int  line_num = 0;

    while (fgets(line, sizeof(line), f)) {
        line_num++;

        /* Strip trailing newline */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char *p = trim(line);

        /* Skip blank lines and comments */
        if (p[0] == '\0' || p[0] == '#') continue;

        /* Split on '=' */
        char *eq = strchr(p, '=');
        if (!eq) {
            fprintf(stderr, "config: line %d: no '=' found, skipped\n",
                    line_num);
            continue;
        }

        *eq = '\0';
        char *key = trim(p);
        char *val = trim(eq + 1);

        /* Dispatch on key */
        if (strcmp(key, "font_path") == 0) {
            strncpy(cfg->font_path, val, CFG_PATH_MAX - 1);

        } else if (strcmp(key, "font_size") == 0) {
            int v = atoi(val);
            if (v >= 8 && v <= 72) cfg->font_size = v;

        } else if (strcmp(key, "win_width") == 0) {
            int v = atoi(val);
            if (v >= 200) cfg->win_width = v;

        } else if (strcmp(key, "win_height") == 0) {
            int v = atoi(val);
            if (v >= 100) cfg->win_height = v;

        } else if (strcmp(key, "fg_color") == 0) {
            int r, g, b;
            if (sscanf(val, "%d,%d,%d", &r, &g, &b) == 3) {
                cfg->fg_r = (uint8_t)r;
                cfg->fg_g = (uint8_t)g;
                cfg->fg_b = (uint8_t)b;
            }

        } else if (strcmp(key, "bg_color") == 0) {
            int r, g, b;
            if (sscanf(val, "%d,%d,%d", &r, &g, &b) == 3) {
                cfg->bg_r = (uint8_t)r;
                cfg->bg_g = (uint8_t)g;
                cfg->bg_b = (uint8_t)b;
            }

        } else if (strcmp(key, "cursor_blink_ms") == 0) {
            int v = atoi(val);
            if (v >= 0) cfg->cursor_blink_ms = v;

        } else if (strcmp(key, "shell") == 0) {
            strncpy(cfg->shell, val, CFG_PATH_MAX - 1);

        } else if (strcmp(key, "scrollback") == 0) {
            int v = atoi(val);
            if (v >= 100 && v <= 50000)
                cfg->scrollback_lines = v;

        } else if (strcmp(key, "tab_width") == 0) {
            int v = atoi(val);
            if (v >= 60 && v <= 400) cfg->tab_width = v;

        } else if (strcmp(key, "tab_bar_height") == 0) {
            int v = atoi(val);
            if (v >= 16 && v <= 60) cfg->tab_bar_height = v;

        } else {
            /* Unknown key — silently ignore */
        }
    }

    fclose(f);
    printf("config: font=%s size=%d win=%dx%d\n",
           cfg->font_path, cfg->font_size,
           cfg->win_width, cfg->win_height);
}