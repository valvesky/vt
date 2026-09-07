#pragma once
/* NOTE(vasco):
 * Currently we depend on stb truetype. 
 *
 */

#include "vt.h"

#include <string.h>

static u16 glyph_generator_internal_be16(const unsigned char *p);
static u32 glyph_generator_internal_be32(const unsigned char *p);
static const unsigned char *glyph_generator_internal_ttf_table(const unsigned char *ttf, unsigned long n, const char *tag, unsigned long *len);
static u32 glyph_generator_internal_cmap_gid(const unsigned char *cmap, unsigned long n, codepoint_t cp);
static int glyph_generator_internal_emoji_png(GlyphGenerator *g, codepoint_t cp, const unsigned char **png, unsigned long *png_n);
static void glyph_generator_internal_slot_clear(Atlas *atlas, u32 slot);
static void glyph_generator_internal_blit_rgba_fit(Atlas *atlas, u32 slot, const u8 *rgba, int w, int h, int cells);

u16
glyph_generator_internal_be16(const unsigned char *p)
{
    return (u16)(((u16)p[0] << 8) | p[1]);
}

u32
glyph_generator_internal_be32(const unsigned char *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

const unsigned char *
glyph_generator_internal_ttf_table(const unsigned char *ttf, unsigned long n, const char *tag, unsigned long *len)
{
    u16 num;
    u16 i;

    if (!ttf || n < 12 || !tag || !len)
        return 0;
    num = glyph_generator_internal_be16(ttf + 4);
    if (n < 12u + (unsigned long)num * 16u)
        return 0;
    for (i = 0; i < num; i++) {
        const unsigned char *rec = ttf + 12 + (unsigned long)i * 16u;
        u32 off;
        u32 size;

        if (memcmp(rec, tag, 4) != 0)
            continue;
        off = glyph_generator_internal_be32(rec + 8);
        size = glyph_generator_internal_be32(rec + 12);
        if ((unsigned long)off + size > n)
            return 0;
        *len = size;
        return ttf + off;
    }
    return 0;
}

u32
glyph_generator_internal_cmap_gid(const unsigned char *cmap, unsigned long n, codepoint_t cp)
{
    u16 nrec;
    u16 i;

    if (!cmap || n < 4)
        return 0;
    nrec = glyph_generator_internal_be16(cmap + 2);
    for (i = 0; i < nrec; i++) {
        u32 ro;
        u16 fmt;

        if (4u + (unsigned long)i * 8u + 8u > n)
            return 0;
        ro = glyph_generator_internal_be32(cmap + 4 + (unsigned long)i * 8u + 4);
        if ((unsigned long)ro + 2u > n)
            continue;
        fmt = glyph_generator_internal_be16(cmap + ro);
        if (fmt != 12)
            continue;
        if ((unsigned long)ro + 16u > n)
            continue;
        u32 ngrp = glyph_generator_internal_be32(cmap + ro + 12);
        u32 g;

        for (g = 0; g < ngrp; g++) {
            const unsigned char *gr = cmap + ro + 16 + g * 12u;
            u32 s;
            u32 e;
            u32 start;

            if ((unsigned long)ro + 16u + (g + 1u) * 12u > n)
                break;
            s = glyph_generator_internal_be32(gr);
            e = glyph_generator_internal_be32(gr + 4);
            start = glyph_generator_internal_be32(gr + 8);
            if (cp >= s && cp <= e)
                return start + (cp - s);
        }
    }
    return 0;
}

int
glyph_generator_internal_emoji_png(GlyphGenerator *g, codepoint_t cp, const unsigned char **png, unsigned long *png_n)
{
    u32 gid;
    u32 num;
    u32 i;

    if (!g || !g->emoji_ttf || !png || !png_n)
        return 0;
    gid = glyph_generator_internal_cmap_gid(g->emoji_cmap, g->emoji_cmap_n, cp);
    if (!gid)
        return 0;
    if (g->emoji_cblc_n < 8)
        return 0;
    num = glyph_generator_internal_be32(g->emoji_cblc + 4);
    for (i = 0; i < num; i++) {
        const unsigned char *b;
        u32 array_off;
        u32 nindex;
        u32 j;

        if (8u + (i + 1u) * 48u > g->emoji_cblc_n)
            return 0;
        b = g->emoji_cblc + 8 + i * 48u;
        array_off = glyph_generator_internal_be32(b);
        nindex = glyph_generator_internal_be32(b + 8);
        for (j = 0; j < nindex; j++) {
            const unsigned char *rec;
            const unsigned char *st;
            u16 first;
            u16 last;
            u32 add_off;
            u16 index_format;
            u16 image_format;
            u32 image_off;

            if (array_off + (j + 1u) * 8u > g->emoji_cblc_n)
                break;
            rec = g->emoji_cblc + array_off + j * 8u;
            first = glyph_generator_internal_be16(rec);
            last = glyph_generator_internal_be16(rec + 2);
            add_off = glyph_generator_internal_be32(rec + 4);
            if (gid < first || gid > last)
                continue;
            st = g->emoji_cblc + array_off + add_off;
            if (st + 8 > g->emoji_cblc + g->emoji_cblc_n)
                return 0;
            index_format = glyph_generator_internal_be16(st);
            image_format = glyph_generator_internal_be16(st + 2);
            image_off = glyph_generator_internal_be32(st + 4);
            if (image_format != 17 || index_format != 1)
                return 0;
            const unsigned char *oa = st + 8 + (u32)(gid - first) * 4u;
            const unsigned char *p;
            u32 off;
            u32 dlen;

            if (oa + 4 > g->emoji_cblc + g->emoji_cblc_n)
                return 0;
            off = glyph_generator_internal_be32(oa);
            p = g->emoji_cbdt + image_off + off;
            if (p + 9 > g->emoji_cbdt + g->emoji_cbdt_n)
                return 0;
            dlen = glyph_generator_internal_be32(p + 5);
            if (p + 9 + dlen > g->emoji_cbdt + g->emoji_cbdt_n)
                return 0;
            *png = p + 9;
            *png_n = dlen;
            return 1;
        }
    }
    return 0;
}

void
glyph_generator_internal_slot_clear(Atlas *atlas, u32 slot)
{
    u32 slot_w = atlas->slot_width ? atlas->slot_width : atlas->cell_width;
    u32 atlas_w = slot_w * atlas->cols;
    u32 cell_x = (slot % atlas->cols) * slot_w;
    u32 cell_y = (slot / atlas->cols) * atlas->cell_height;
    u32 py;

    for (py = 0; py < atlas->cell_height; py++) {
        u8 *row = atlas->atlas + ((cell_y + py) * atlas_w + cell_x) * 4u;
        memset(row, 0, (size_t)slot_w * 4u);
    }
}

void
glyph_generator_internal_blit_rgba_fit(Atlas *atlas, u32 slot, const u8 *rgba, int w, int h, int cells)
{
    int sw;
    int ch;
    int tw;
    int dw;
    int dh;
    int ox;
    int oy;
    int atlas_w;
    int cell_x;
    int cell_y;
    int stride;
    int y;
    int x;

    if (!atlas->atlas || !rgba || w <= 0 || h <= 0)
        return;
    if (cells < 1)
        cells = 1;
    sw = (int)atlas->slot_width;
    ch = (int)atlas->cell_height;
    tw = (int)atlas->cell_width * cells;
    if (tw > sw)
        tw = sw;
    glyph_generator_internal_slot_clear(atlas, slot);
    if (w * ch > h * tw) {
        dw = tw;
        dh = h * tw / w;
        if (dh < 1)
            dh = 1;
    } else {
        dh = ch;
        dw = w * ch / h;
        if (dw < 1)
            dw = 1;
    }
    ox = (tw - dw) / 2;
    oy = (ch - dh) / 2;
    atlas_w = sw * (int)atlas->cols;
    cell_x = (int)(slot % atlas->cols) * sw;
    cell_y = (int)(slot / atlas->cols) * ch;
    stride = atlas_w * 4;
    for (y = 0; y < dh; y++) {
        int sy0 = y * h / dh;
        int sy1 = (y + 1) * h / dh;

        if (sy1 <= sy0)
            sy1 = sy0 + 1;
        for (x = 0; x < dw; x++) {
            int sx0 = x * w / dw;
            int sx1 = (x + 1) * w / dw;
            int r = 0;
            int g = 0;
            int b = 0;
            int a = 0;
            int n = 0;
            int sy;
            int sx;
            u8 *dst;

            if (sx1 <= sx0)
                sx1 = sx0 + 1;
            for (sy = sy0; sy < sy1; sy++) {
                for (sx = sx0; sx < sx1; sx++) {
                    const u8 *p = rgba + ((sy * w) + sx) * 4;

                    r += p[0];
                    g += p[1];
                    b += p[2];
                    a += p[3];
                    n++;
                }
            }
            if (!n)
                continue;
            dst = atlas->atlas + (cell_y + oy + y) * stride + (cell_x + ox + x) * 4;
            dst[0] = (u8)(r / n);
            dst[1] = (u8)(g / n);
            dst[2] = (u8)(b / n);
            dst[3] = (u8)(a / n);
        }
    }
}

int
glyph_generator_set_font(GlyphGenerator *g, const char *path, float px)
{
    if (!g || !path || px <= 0.f)
        return 0;

    unsigned long n = 0;
    unsigned char *ttf = peak_file_alloc(path, &n);
    if (!ttf || !n)
        return 0;

    stbtt_fontinfo font;
    memset(&font, 0, sizeof font);
    if (!stbtt_InitFont(&font, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        free(ttf);
        return 0;
    }

    float scale = stbtt_ScaleForPixelHeight(&font, px);
    int ascent;
    int descent;
    int line_gap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    u32 cell_h = (u32)(scale * (float)(ascent - descent) + 0.5f);
    int adv;
    int lsb;
    stbtt_GetCodepointHMetrics(&font, 'M', &adv, &lsb);
    u32 cell_w = (u32)(scale * (float)adv + 0.5f);
    if (!cell_w)
        cell_w = (u32)(px + 0.5f);
    if (!cell_h)
        cell_h = (u32)(px + 0.5f);

    u8 *scratch = malloc((size_t)cell_w * (size_t)cell_h);
    if (!scratch) {
        free(ttf);
        return 0;
    }
    free(g->ttf);
    free(g->scratch);
    g->ttf = ttf;
    g->scratch = scratch;
    g->ttf_n = n;
    g->font = font;
    g->pixel_height = px;
    g->scale = scale;
    g->ascent = (int)(scale * (float)ascent);
    g->cell_w = cell_w;
    g->cell_h = cell_h;
    return 1;
}

int
glyph_generator_set_fallback(GlyphGenerator *g, const char *path)
{
    if (!g || !path || g->pixel_height <= 0.f)
        return 0;

    unsigned long n = 0;
    unsigned char *ttf = peak_file_alloc(path, &n);
    if (!ttf || !n)
        return 0;

    stbtt_fontinfo font;
    memset(&font, 0, sizeof font);
    if (!stbtt_InitFont(&font, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        free(ttf);
        return 0;
    }

    float scale = stbtt_ScaleForPixelHeight(&font, g->pixel_height);
    int ascent;
    stbtt_GetFontVMetrics(&font, &ascent, 0, 0);
    free(g->fallback_ttf);
    g->fallback_ttf = ttf;
    g->fallback_ttf_n = n;
    g->fallback_font = font;
    g->fallback_scale = scale;
    g->fallback_ascent = (int)(scale * (float)ascent);
    return 1;
}

int
glyph_generator_set_emoji(GlyphGenerator *g, const char *path)
{
    if (!g || !path || !path[0])
        return 0;

    unsigned long n = 0;
    unsigned char *ttf = peak_file_alloc(path, &n);
    if (!ttf || !n)
        return 0;

    unsigned long cmap_n = 0;
    unsigned long cblc_n = 0;
    unsigned long cbdt_n = 0;
    const unsigned char *cmap = glyph_generator_internal_ttf_table(ttf, n, "cmap", &cmap_n);
    const unsigned char *cblc = glyph_generator_internal_ttf_table(ttf, n, "CBLC", &cblc_n);
    const unsigned char *cbdt = glyph_generator_internal_ttf_table(ttf, n, "CBDT", &cbdt_n);
    if (!cmap || !cblc || !cbdt) {
        free(ttf);
        return 0;
    }
    free(g->emoji_ttf);
    g->emoji_ttf = ttf;
    g->emoji_ttf_n = n;
    g->emoji_cmap = cmap;
    g->emoji_cblc = cblc;
    g->emoji_cbdt = cbdt;
    g->emoji_cmap_n = cmap_n;
    g->emoji_cblc_n = cblc_n;
    g->emoji_cbdt_n = cbdt_n;
    return 1;
}

void
glyph_generator_destroy(GlyphGenerator *g)
{
    free(g->ttf);
    free(g->fallback_ttf);
    free(g->emoji_ttf);
    free(g->scratch);
    memset(g, 0, sizeof *g);
}

int
glyph_generator_rasterize_codepoint(GlyphGenerator *g, Atlas *atlas, u32 slot, codepoint_t cp)
{
    u32 slot_w = atlas->slot_width ? atlas->slot_width : atlas->cell_width;
    u32 atlas_w = slot_w * atlas->cols;
    if (slot >= atlas->cols * atlas->rows)
        return 0;

    if (cp >= 128 && !(cp >= 0x2500 && cp <= 0x259F) && g->emoji_ttf) {
        const unsigned char *png;
        unsigned long png_n;
        u8 *rgba;
        int w;
        int h;
        int n;

        if (glyph_generator_internal_emoji_png(g, cp, &png, &png_n)) {
            rgba = stbi_load_from_memory(png, (int)png_n, &w, &h, &n, 4);
            if (rgba) {
                int cells = (cp >= 0x1F300 && cp <= 0x1FAFF) ? 2 : 1;

                glyph_generator_internal_blit_rgba_fit(atlas, slot, rgba, w, h, cells);
                stbi_image_free(rgba);
                return 1;
            }
        }
    }

    glyph_generator_internal_slot_clear(atlas, slot);
    u32 cell_x = (slot % atlas->cols) * slot_w;
    u32 cell_y = (slot / atlas->cols) * atlas->cell_height;

    stbtt_fontinfo *font = &g->font;
    float scale = g->scale;
    int ascent = g->ascent;
    int gi = stbtt_FindGlyphIndex(&g->font, (int)cp);
    if (!gi && g->fallback_ttf) {
        int fallback_gi = stbtt_FindGlyphIndex(&g->fallback_font, (int)cp);
        if (fallback_gi) {
            gi = fallback_gi;
            font = &g->fallback_font;
            scale = g->fallback_scale;
            ascent = g->fallback_ascent;
        }
    }

    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(font, gi, scale, scale, &x0, &y0, &x1, &y1);
    int bitmap_w = x1 - x0;
    int bitmap_h = y1 - y0;
    if (bitmap_w <= 0 || bitmap_h <= 0)
        return 0;
    if (bitmap_w > (int)g->cell_w)
        bitmap_w = (int)g->cell_w;
    if (bitmap_h > (int)g->cell_h)
        bitmap_h = (int)g->cell_h;

    u8 *bitmap = g->scratch;
    stbtt_MakeGlyphBitmap(font, bitmap, bitmap_w, bitmap_h, bitmap_w, scale, scale, gi);

    int dest_x = x0;
    int dest_y = ascent + y0;
    for (int by = 0; by < bitmap_h; by++) {
        int dy = dest_y + by;
        if (dy < 0 || (u32)dy >= atlas->cell_height)
            continue;
        for (int bx = 0; bx < bitmap_w; bx++) {
            int dx = dest_x + bx;
            if (dx < 0 || (u32)dx >= atlas->cell_width)
                continue;
            u8 cover = bitmap[by * bitmap_w + bx];
            u8 *px = atlas->atlas + ((cell_y + (u32)dy) * atlas_w + cell_x + (u32)dx) * 4u;
            px[0] = cover;
            px[1] = 0;
            px[2] = 0;
            px[3] = cover;
        }
    }
    return 0;
}

void
glyph_generator_rasterize_run(GlyphGenerator *g, Atlas *atlas, u32 slot, const codepoint_t *cps, u32 n)
{
}
