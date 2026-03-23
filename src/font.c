#define _GNU_SOURCE

#include "font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * font_init — loads a .ttf file and pre-renders all printable
 * ASCII characters into GPU textures.
 *
 * Parameters:
 *   f        — pointer to our Font struct
 *   renderer — the SDL2 renderer (needed to create textures)
 *   path     — file path to the .ttf font file
 *   size     — font size in pixels (e.g. 16)
 *
 * Returns 0 on success, -1 on failure.
 */
int font_init(Font *f, SDL_Renderer *renderer,
              const char *path, int size) {

    /* Zero out the struct so all pointers start as NULL.
     * memset fills a block of memory with a given value.
     * sizeof(Font) = total bytes the Font struct occupies. */
    memset(f, 0, sizeof(Font));
    f->size = size;

    /* Step 1: Initialize the FreeType library */
    if (FT_Init_FreeType(&f->library) != 0) {
        printf("FreeType init failed\n");
        return -1;
    }

    /* Step 2: Load the font file into a "face"
     * A face = one font + one style (e.g. Regular, Bold)
     * The 0 at the end = use the first face in the file */
    if (FT_New_Face(f->library, path, 0, &f->face) != 0) {
        printf("Failed to load font: %s\n", path);
        FT_Done_FreeType(f->library);
        return -1;
    }

    /* Step 3: Set the font size.
     * FT_Set_Pixel_Sizes(face, width, height)
     * 0 for width = auto-calculate from height */
    FT_Set_Pixel_Sizes(f->face, 0, size);

    /* Step 4: Pre-render all printable ASCII characters.
     * ASCII 32 = space, 127 = DEL (we stop before that).
     * We loop through every printable character and cache it. */
    for (int c = 32; c < GLYPH_COUNT; c++) {

        /* FT_LOAD_RENDER = load the glyph AND rasterize it.
         * After this call, f->face->glyph->bitmap holds pixels. */
        if (FT_Load_Char(f->face, c, FT_LOAD_RENDER) != 0) {
            printf("Failed to load glyph '%c'\n", c);
            continue;  /* Skip this character, don't crash */
        }

        FT_GlyphSlot slot = f->face->glyph;
        FT_Bitmap   *bmp  = &slot->bitmap;

        /* Store the positioning metrics.
         * advance.x is in 26.6 fixed-point format (1/64 pixels).
         * Shifting right by 6 divides by 64, giving us real pixels. */
        f->cache[c].bearing_x = slot->bitmap_left;
        f->cache[c].bearing_y = slot->bitmap_top;
        f->cache[c].advance   = slot->advance.x >> 6;
        f->cache[c].width     = bmp->width;
        f->cache[c].height    = bmp->rows;

        /* Some characters (like space) have no visible pixels.
         * Skip creating a texture for them — they have no bitmap. */
        if (bmp->width == 0 || bmp->rows == 0) {
            continue;
        }

        /*
         * FreeType gives us a GRAYSCALE bitmap: one byte per pixel,
         * 0 = transparent, 255 = fully opaque.
         *
         * SDL2 needs RGBA: 4 bytes per pixel.
         * We write bytes in order: R=255, G=255, B=255, A=glyph_alpha.
         *
         * IMPORTANT: We use SDL_PIXELFORMAT_ABGR8888 below.
         * On Linux, SDL stores pixels in memory as B,G,R,A but the
         * format name is from the GPU's perspective (reversed).
         * ABGR8888 matches our byte order: [R][G][B][A] in memory.
         * Using RGBA8888 here would swap red and blue channels,
         * making all text appear with wrong colors.
         */
        Uint8 *rgba = malloc(bmp->width * bmp->rows * 4);
        if (!rgba) {
            printf("Out of memory allocating glyph buffer\n");
            return -1;
        }

        for (int i = 0; i < (int)(bmp->width * bmp->rows); i++) {
            rgba[i * 4 + 0] = 255;             /* Red   channel = full */
            rgba[i * 4 + 1] = 255;             /* Green channel = full */
            rgba[i * 4 + 2] = 255;             /* Blue  channel = full */
            rgba[i * 4 + 3] = bmp->buffer[i];  /* Alpha from FreeType  */
        }

        /*
         * Create an SDL2 texture on the GPU.
         * SDL_PIXELFORMAT_ABGR8888 matches our [R,G,B,A] byte layout.
         * SDL_TEXTUREACCESS_STATIC = upload once, read many times.
         */
        SDL_Texture *tex = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_ABGR8888,      /* FIX: was RGBA8888 */
            SDL_TEXTUREACCESS_STATIC,
            bmp->width,
            bmp->rows
        );

        if (!tex) {
            printf("SDL_CreateTexture failed for '%c': %s\n",
                   c, SDL_GetError());
            free(rgba);
            continue;
        }

        /* Upload pixel data into the texture.
         * pitch = bytes per row = width * 4 bytes per pixel */
        SDL_UpdateTexture(tex, NULL, rgba, bmp->width * 4);

        /* Enable alpha blending so glyph edges blend with the background */
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

        free(rgba); /* CPU copy no longer needed — GPU has it now */

        f->cache[c].texture = tex;
    }

    /*
     * Determine cell dimensions — how much space one character occupies.
     *
     * cell_width:  use the advance of 'M' — widest typical character.
     *              In a true monospace font every character has the same
     *              advance, so any character would give the same result.
     *
     * cell_height: use FreeType's own line height metric.
     *              face->size->metrics.height is in 26.6 fixed-point,
     *              so we shift right 6 to get real pixels.
     *              This is more accurate than (size + padding) guessing.
     */
    f->cell_width  = f->cache['M'].advance;
    f->cell_height = (int)(f->face->size->metrics.height >> 6);

    /* Safety fallback in case metrics return zero (rare but possible) */
    if (f->cell_height <= 0) f->cell_height = size + 4;
    if (f->cell_width  <= 0) f->cell_width  = size / 2;

    printf("Font loaded: %s @ %dpx\n", path, size);
    printf("Cell size: %dx%d px\n", f->cell_width, f->cell_height);

    return 0;  /* FIX: was missing — function must return int */
}

/*
 * font_draw_char — draws a single character at pixel position (x, y).
 * x, y is the TOP-LEFT corner of the character cell.
 * r, g, b controls the text color (0–255 each).
 */
void font_draw_char(Font *f, SDL_Renderer *renderer,
                    char c, int x, int y,
                    Uint8 r, Uint8 g, Uint8 b) {

    /* Cast to unsigned to safely use as array index */
    unsigned char uc = (unsigned char)c;

    /* Only handle printable ASCII range */
    if (uc < 32 || uc >= GLYPH_COUNT) return;

    Glyph *glyph = &f->cache[uc];

    /* Space and zero-bitmap chars have no texture — nothing to draw */
    if (glyph->texture == NULL) return;

    /*
     * SDL_SetTextureColorMod multiplies the texture's RGB values by r,g,b.
     * Our texture is white (255,255,255), so:
     *   white * (r,g,b) = (r,g,b)
     * This lets us reuse one white texture for any color without
     * creating separate colored textures.
     */
    SDL_SetTextureColorMod(glyph->texture, r, g, b);

    /*
     * Position the glyph using FreeType bearing metrics.
     *
     * bearing_x = horizontal offset from cell left edge to glyph left.
     *             Positive = shift right (most chars).
     *
     * bearing_y = distance from baseline UP to glyph top.
     *             e.g. 'h' has high bearing_y, 'g' has low (descends below).
     *
     * We position y so the glyph sits correctly on the baseline.
     * The baseline sits at (y + cell_height - descender_space).
     * A simple reliable formula: y + (cell_height - bearing_y).
     */
    SDL_Rect dst = {
        x + glyph->bearing_x,
        y + (f->cell_height - glyph->bearing_y),
        glyph->width,
        glyph->height
    };

    SDL_RenderCopy(renderer, glyph->texture, NULL, &dst);
}

/*
 * font_draw_string — draws a full null-terminated string at (x, y).
 * Advances the cursor by each character's advance width.
 */
void font_draw_string(Font *f, SDL_Renderer *renderer,
                      const char *str, int x, int y,
                      Uint8 r, Uint8 g, Uint8 b) {

    int cursor_x = x;

    /*
     * In C, a string is an array of chars ending with '\0' (value 0).
     * *str dereferences the pointer to get the current character.
     * str++ moves the pointer forward by one byte (one character).
     * The loop stops when *str == '\0' (end of string).
     */
    while (*str != '\0') {
        font_draw_char(f, renderer, *str, cursor_x, y, r, g, b);

        /* Advance cursor rightward by this character's advance width */
        unsigned char uc = (unsigned char)*str;
        if (uc >= 32 && uc < GLYPH_COUNT) {
            cursor_x += f->cache[uc].advance;
        }

        str++;
    }
}

/*
 * font_destroy — free all GPU textures and FreeType resources.
 *
 * Rule: always free in REVERSE order of allocation.
 * We allocated: library → face → textures
 * We free:      textures → face → library
 */
void font_destroy(Font *f) {
    if (!f) return;

    for (int c = 32; c < GLYPH_COUNT; c++) {
        if (f->cache[c].texture) {
            SDL_DestroyTexture(f->cache[c].texture);
            f->cache[c].texture = NULL;
        }
    }

    if (f->face)    FT_Done_Face(f->face);
    if (f->library) FT_Done_FreeType(f->library);

    printf("Font destroyed.\n");
}