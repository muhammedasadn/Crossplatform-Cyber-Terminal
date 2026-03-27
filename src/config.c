/*
 * config.c — Configuration file loader for cterm.
 *
 * Reads ~/.config/cterm/cterm.conf at startup.
 * Creates a default config file if none exists.
 *
 * Parser is intentionally simple:
 *   - One key = value pair per line
 *   - Leading/trailing whitespace stripped
 *   - Lines starting with # are comments
 *   - Unknown keys are silently ignored
 *   - Malformed values fall back to defaults
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

/* ── Global config instance ─────────────────────────────────── */
Config g_config;

/* ── Default font search paths ──────────────────────────────── */
/*
 * We try several common font locations on Ubuntu/Debian.
 * The first one that exists is used as the default.
 */
static const char *FONT_CANDIDATES[] = {
    /* JetBrains Mono — best terminal font */
    "/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMonoNL-Regular.ttf",
    "/usr/share/fonts/truetype/jetbrains/JetBrainsMono-Regular.ttf",
    /* DejaVu — always present on Ubuntu */
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    /* Ubuntu Mono */
    "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
    /* Liberation Mono — Red Hat/Fedora */
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    /* Courier New */
    "/usr/share/fonts/truetype/msttcorefonts/cour.ttf",
    /* Last resort — project assets folder */
    "../assets/font.ttf",
    NULL
};

/* ── set_defaults ───────────────────────────────────────────── */
static void set_defaults(void) {
    /* Font: search for best available */
    g_config.font_path[0] = '\0';
    for (int i = 0; FONT_CANDIDATES[i] != NULL; i++) {
        if (access(FONT_CANDIDATES[i], R_OK) == 0) {
            strncpy(g_config.font_path, FONT_CANDIDATES[i],
                    CONFIG_PATH_MAX - 1);
            break;
        }
    }
    /* Absolute fallback */
    if (g_config.font_path[0] == '\0') {
        strncpy(g_config.font_path, "../assets/font.ttf",
                CONFIG_PATH_MAX - 1);
    }

    g_config.font_size        = 16;
    g_config.font_antialiasing = 1;

    /* Colors */
    g_config.fg_r = 255; g_config.fg_g = 255; g_config.fg_b = 255;
    g_config.bg_r = 18;  g_config.bg_g = 18;  g_config.bg_b = 18;
    g_config.cursor_r = 220; g_config.cursor_g = 220; g_config.cursor_b = 220;

    /* Window */
    g_config.window_width    = 900;
    g_config.window_height   = 560;
    g_config.start_fullscreen = 0;

    /* Terminal */
    g_config.scrollback_lines = 5000;
}

/* ── trim ───────────────────────────────────────────────────── */
/* Remove leading and trailing whitespace in-place */
static char *trim(char *s) {
    /* Leading */
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    /* Trailing */
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return s;
}

/* ── get_config_path ────────────────────────────────────────── */
static void get_config_path(char *out, int size) {
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(out, size, "%s/.config/cterm/cterm.conf", home);
}

/* ── get_config_dir ─────────────────────────────────────────── */
static void get_config_dir(char *out, int size) {
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(out, size, "%s/.config/cterm", home);
}

/* ── parse_line ─────────────────────────────────────────────── */
static void parse_line(char *line) {
    /* Skip comments and blank lines */
    char *trimmed = trim(line);
    if (*trimmed == '\0' || *trimmed == '#') return;

    /* Split on '=' */
    char *eq = strchr(trimmed, '=');
    if (!eq) return;

    *eq = '\0';
    char *key = trim(trimmed);
    char *val = trim(eq + 1);

    if (!*key || !*val) return;

    /* Match keys */
    if (strcmp(key, "font_path") == 0) {
        strncpy(g_config.font_path, val, CONFIG_PATH_MAX - 1);

    } else if (strcmp(key, "font_size") == 0) {
        int v = atoi(val);
        if (v >= 6 && v <= 72) g_config.font_size = v;

    } else if (strcmp(key, "font_antialiasing") == 0) {
        g_config.font_antialiasing = atoi(val) ? 1 : 0;

    } else if (strcmp(key, "fg_color") == 0) {
        sscanf(val, "%d %d %d",
               &g_config.fg_r, &g_config.fg_g, &g_config.fg_b);

    } else if (strcmp(key, "bg_color") == 0) {
        sscanf(val, "%d %d %d",
               &g_config.bg_r, &g_config.bg_g, &g_config.bg_b);

    } else if (strcmp(key, "cursor_color") == 0) {
        sscanf(val, "%d %d %d",
               &g_config.cursor_r,
               &g_config.cursor_g,
               &g_config.cursor_b);

    } else if (strcmp(key, "window_width") == 0) {
        int v = atoi(val);
        if (v >= 200) g_config.window_width = v;

    } else if (strcmp(key, "window_height") == 0) {
        int v = atoi(val);
        if (v >= 100) g_config.window_height = v;

    } else if (strcmp(key, "start_fullscreen") == 0) {
        g_config.start_fullscreen = atoi(val) ? 1 : 0;

    } else if (strcmp(key, "scrollback") == 0) {
        int v = atoi(val);
        if (v >= 100 && v <= 50000)
            g_config.scrollback_lines = v;
    }
    /* Unknown keys silently ignored */
}

/* ── config_load ────────────────────────────────────────────── */
void config_load(void) {
    /* Always start with safe defaults */
    set_defaults();

    char path[512];
    get_config_path(path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        /* No config file — use defaults and create one */
        printf("config: no config file found, using defaults\n");
        printf("config: creating default at %s\n", path);
        config_save_default();
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        parse_line(line);
    }

    fclose(f);
    printf("config: loaded from %s\n", path);
}

/* ── config_save_default ────────────────────────────────────── */
void config_save_default(void) {
    char dir[512];
    get_config_dir(dir, sizeof(dir));

    /* Create directory if it doesn't exist */
    struct stat st;
    if (stat(dir, &st) != 0) {
        /* Try creating ~/.config first, then ~/.config/cterm */
        char parent[512];
        const char *home = getenv("HOME");
        if (!home) return;
        snprintf(parent, sizeof(parent), "%s/.config", home);
        mkdir(parent, 0755);
        mkdir(dir, 0755);
    }

    char path[512];
    get_config_path(path, sizeof(path));

    /* Don't overwrite an existing file */
    if (access(path, F_OK) == 0) return;

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "config: cannot write to %s: %s\n",
                path, strerror(errno));
        return;
    }

    fprintf(f,
        "# cterm configuration file\n"
        "# ~/.config/cterm/cterm.conf\n"
        "#\n"
        "# Lines starting with # are comments.\n"
        "# Edit and restart cterm to apply changes.\n"
        "\n"
        "# ── Font ────────────────────────────────────────\n"
        "# Path to a monospace .ttf font file.\n"
        "# Leave commented to auto-detect.\n"
        "# font_path = /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf\n"
        "font_size  = %d\n"
        "\n"
        "# ── Colors (R G B, each 0-255) ──────────────────\n"
        "fg_color      = %d %d %d\n"
        "bg_color      = %d %d %d\n"
        "cursor_color  = %d %d %d\n"
        "\n"
        "# ── Window ───────────────────────────────────────\n"
        "window_width   = %d\n"
        "window_height  = %d\n"
        "# start_fullscreen = 0\n"
        "\n"
        "# ── Terminal ─────────────────────────────────────\n"
        "# Number of lines kept in scrollback history.\n"
        "scrollback = %d\n",
        g_config.font_size,
        g_config.fg_r, g_config.fg_g, g_config.fg_b,
        g_config.bg_r, g_config.bg_g, g_config.bg_b,
        g_config.cursor_r, g_config.cursor_g, g_config.cursor_b,
        g_config.window_width, g_config.window_height,
        g_config.scrollback_lines
    );

    fclose(f);
    printf("config: default config written to %s\n", path);
}

/* ── config_print ───────────────────────────────────────────── */
void config_print(void) {
    printf("=== cterm config ===\n");
    printf("font_path    : %s\n", g_config.font_path);
    printf("font_size    : %d\n", g_config.font_size);
    printf("fg_color     : %d %d %d\n",
           g_config.fg_r, g_config.fg_g, g_config.fg_b);
    printf("bg_color     : %d %d %d\n",
           g_config.bg_r, g_config.bg_g, g_config.bg_b);
    printf("window       : %dx%d\n",
           g_config.window_width, g_config.window_height);
    printf("scrollback   : %d\n", g_config.scrollback_lines);
    printf("fullscreen   : %d\n", g_config.start_fullscreen);
    printf("====================\n");
}