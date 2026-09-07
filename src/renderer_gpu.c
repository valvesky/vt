#pragma once
#include "config.h"

#include <stddef.h>
#include "vt.h"
#include "renderer_cpu.c"

static const RendVertexAttributes renderer_cell_attrs[] = {
    { .location = 0, .binding = 0, .offset = offsetof(VtInstance, pos), .format = REND_FORMAT_R32_UINT },
    { .location = 1, .binding = 0, .offset = offsetof(VtInstance, foreground), .format = REND_FORMAT_R32_UINT },
    { .location = 2, .binding = 0, .offset = offsetof(VtInstance, background), .format = REND_FORMAT_R32_UINT },
};

static void *renderer_file_alloc(const char *rel, unsigned long *n);
static u32 renderer_glyph_pack(u32 slot, u32 cols);
static u32 renderer_glyph_slot(u32 packed, u32 cols);
static u32 renderer_glyph_get(Renderer *r, codepoint_t cp);
static void renderer_unpack_rgb(u32 packed, u8 *r, u8 *g, u8 *b);
static void renderer_bake_colors(u32 *fg, u32 *bg);
static void renderer_upload_atlas(Renderer *r);
static void renderer_instance_pack(VtInstance *inst, u32 pos, u32 g, color_packed_t fg, color_packed_t bg, int wide);

void *
renderer_file_alloc(const char *rel, unsigned long *n)
{
    void *p;
    char buf[256];

    p = peak_file_alloc(rel, n);
    if (p)
        return p;
    if (snprintf(buf, sizeof buf, "/usr/share/vt/%s", rel) < 0)
        return 0;
    return peak_file_alloc(buf, n);
}

u32
renderer_glyph_pack(u32 slot, u32 cols)
{
    if (!cols)
        return 0;
    return ((slot % cols) << 16) | (slot / cols);
}

u32
renderer_glyph_slot(u32 packed, u32 cols)
{
    return (packed & 0x7fu) * cols + ((packed >> 16) & 0xffffu);
}

u32
renderer_glyph_get(Renderer *r, codepoint_t cp)
{
    GlyphState st;
    u32 packed;
    u32 slot;

    if (!r || !r->glyph_table || !r->atlas.atlas)
        return 0;
    if (cp >= GLYPH_DIRECT_LO && cp <= GLYPH_DIRECT_HI)
        return renderer_glyph_pack(cp - GLYPH_DIRECT_LO, r->atlas.cols);
    if (cp == 0xFFFDu)
        return renderer_glyph_pack(GLYPH_DIRECT_FFFD, r->atlas.cols);
    st = glyph_table_peek_hash(r->glyph_table, glyph_hash(cp));
    if (st.id && (st.filled_state & GLYPH_FILLED))
        return st.gpu_idx.value | ((st.filled_state & GLYPH_FILLED_COLOR) ? GLYPH_COLOR : 0);
    st = glyph_table_find_hash(r->glyph_table, glyph_hash(cp));
    packed = st.gpu_idx.value;
    if (st.filled_state & GLYPH_FILLED)
        return packed | ((st.filled_state & GLYPH_FILLED_COLOR) ? GLYPH_COLOR : 0);
    slot = renderer_glyph_slot(packed, r->atlas.cols);
    int color = glyph_generator_rasterize_codepoint(&r->generator, &r->atlas, slot, cp);
    glyph_table_update_entry(r->glyph_table, st.id, GLYPH_FILLED | (color ? GLYPH_FILLED_COLOR : 0), 1, 1);
    r->atlas_dirty = 1;
    return packed | (color ? GLYPH_COLOR : 0);
}

void
renderer_instance_pack(VtInstance *inst, u32 pos, u32 g, color_packed_t fg, color_packed_t bg, int wide)
{
    inst->pos = pos;
    inst->foreground = (fg & 0xffffff00u)
        | (((g >> 16) & 31u) << 2)
        | ((g & GLYPH_COLOR) ? 0x80u : 0)
        | ((fg >> 3) & 1u) | ((fg >> 6) & 2u);
    inst->background = (bg & 0xffffff00u) | (g & 0x7fu) | (wide ? 0x80u : 0);
}

int
renderer_instance_resize(Renderer *r, u32 cols, u32 rows)
{
    u32 cap;
    RendBufferType type;

    if (!r || !r->gpu || !cols || !rows)
        return 0;
    cap = cols * rows;
    if (r->instance_cols == cols && r->instance_rows == rows && rend_buffer_mapped(&r->instance))
        return 1;
    if (r->instance_cap)
        rend_buffer_destroy(&r->instance);
    type = r->tile ? REND_BUFFER_STORAGE : REND_BUFFER_VERTEX;
    r->instance = rend_buffer_create(r->gpu, (size_t)cap * sizeof (VtInstance), type, false);
    if (!rend_buffer_mapped(&r->instance)) {
        r->instance_cap = 0;
        r->instance_cols = 0;
        r->instance_rows = 0;
        return 0;
    }
    r->instance_cap = cap;
    r->instance_cols = cols;
    r->instance_rows = rows;
    if (r->tile)
        rend_descriptor_write_ssbo(r->gpu, r->instance, 1, 0);
    return 1;
}

u32
renderer_fill_cp(Renderer *r, u32 x, u32 y, codepoint_t cp, color_packed_t fg, color_packed_t bg, VtInstance *inst, u32 n, u32 cap)
{
    u32 g;

    if (!r || !inst || !cp)
        return n;
    if (fg & (TERM_ATTR_REVERSE | TERM_ATTR_INVISIBLE | TERM_ATTR_BOLD | TERM_ATTR_FAINT))
        renderer_bake_colors(&fg, &bg);
    g = renderer_glyph_get(r, cp);
    if (r->tile) {
        u32 i;

        i = y * r->instance_cols + x;
        if (i >= r->instance_cap)
            return n;
        renderer_instance_pack(&inst[i], 0, g, fg, bg, 0);
        return n;
    }
    if (n >= cap)
        return n;
    renderer_instance_pack(&inst[n], (x << 16) | y, g, fg, bg, 0);
    return n + 1;
}

u32
renderer_fill_term(Renderer *r, Term *t, u32 ox, u32 oy, VtInstance *inst, u32 n, u32 cap, int cursor)
{
    TermScreen *s;
    u32 x;
    u32 y;
    u32 def_bg;
    u32 def_fg;
    int cursor_on;

    if (!r || !t || !inst)
        return n;
    s = term_screen(t);
    if (!s || !s->line || !s->cols || !s->rows)
        return n;
    def_bg = t->colors.bg[t->colors.bg_default < 8 ? t->colors.bg_default : 0] << 8;
    def_fg = t->colors.fg[t->colors.fg_default < 16 ? t->colors.fg_default : 7] << 8;
    cursor_on = cursor && !(t->mode & TERM_MODE_HIDE);
    for (y = 0; y < s->rows; y++) {
        TermCell *row;
        int skip_wide;

        row = term_row(s, y);
        if (!row)
            continue;
        skip_wide = 0;
        for (x = 0; x < s->cols; x++) {
            TermCell *cell;
            TermStyle st;
            codepoint_t cp;
            color_packed_t fg;
            color_packed_t bg;
            int at_cursor;
            int selected;
            int wide;
            u32 g;

            cell = &row[x];
            at_cursor = cursor_on && x == t->cursor.x && y == t->cursor.y;
            selected = 0;
            if (r->sel_on && r->mux_cols) {
                u32 gidx;

                gidx = (oy + y) * r->mux_cols + (ox + x);
                selected = gidx >= r->sel0 && gidx <= r->sel1;
            }
            cp = cell->codepoint;
            st = term_cell_style(t, cell);
            fg = st.fg;
            bg = st.bg;
            if (!cp && !fg && !bg && !at_cursor && !selected)
                continue;
            if (!fg && !bg) {
                fg = def_fg;
                bg = def_bg;
            }
            if (selected && !at_cursor) {
                color_packed_t tmp;

                tmp = fg;
                fg = bg;
                bg = tmp;
                if (!cp)
                    cp = (codepoint_t)' ';
            }
            if (at_cursor) {
                color_packed_t tmp;

                tmp = fg;
                fg = bg;
                bg = tmp;
                if (!cp)
                    cp = (codepoint_t)' ';
            }
            if (!cp) {
                if (skip_wide && !at_cursor && !selected) {
                    skip_wide = 0;
                    continue;
                }
                cp = (codepoint_t)' ';
            }
            skip_wide = 0;
            if (cp == (codepoint_t)' '
                    && !at_cursor && !selected
                    && !(fg & (TERM_ATTR_UNDERLINE | TERM_ATTR_STRUCK | TERM_ATTR_REVERSE))
                    && (bg & 0xffffff00u) == def_bg)
                continue;
            if (fg & (TERM_ATTR_REVERSE | TERM_ATTR_INVISIBLE | TERM_ATTR_BOLD | TERM_ATTR_FAINT))
                renderer_bake_colors(&fg, &bg);
            g = renderer_glyph_get(r, cp);
            wide = (g & GLYPH_COLOR) && x + 1u < s->cols && row[x + 1u].codepoint == 0;
            if (r->tile) {
                u32 i;

                i = (y + oy) * r->instance_cols + (x + ox);
                if (i < r->instance_cap) {
                    renderer_instance_pack(&inst[i], 0, g, fg, bg, 0);
                    if (wide && i + 1u < r->instance_cap) {
                        inst[i + 1u] = inst[i];
                        inst[i + 1u].pos = 1;
                        skip_wide = 1;
                    }
                }
            } else {
                if (n >= cap)
                    return n;
                renderer_instance_pack(&inst[n], ((x + ox) << 16) | (y + oy), g, fg, bg, wide);
                n++;
                if (wide)
                    skip_wide = 1;
            }
        }
    }
    return n;
}

void
renderer_unpack_rgb(u32 packed, u8 *r, u8 *g, u8 *b)
{
    *r = (u8)((packed >> 24) & 0xff);
    *g = (u8)((packed >> 16) & 0xff);
    *b = (u8)((packed >> 8) & 0xff);
}

void
renderer_bake_colors(u32 *fg, u32 *bg)
{
    u8 fr, fg8, fb, br, bg8, bb;
    u8 attr;

    attr = (u8)(*fg & 0xff);
    if (!(attr & (TERM_ATTR_REVERSE | TERM_ATTR_INVISIBLE | TERM_ATTR_BOLD | TERM_ATTR_FAINT))) {
        *fg = (*fg & 0xffffff00u) | (attr & (u8)(TERM_ATTR_UNDERLINE | TERM_ATTR_STRUCK));
        *bg = *bg & 0xffffff00u;
        return;
    }
    renderer_unpack_rgb(*fg, &fr, &fg8, &fb);
    renderer_unpack_rgb(*bg, &br, &bg8, &bb);
    if (attr & TERM_ATTR_REVERSE) {
        u8 tr = fr, tg = fg8, tb = fb;
        fr = br; fg8 = bg8; fb = bb;
        br = tr; bg8 = tg; bb = tb;
    }
    if (attr & TERM_ATTR_INVISIBLE) {
        fr = br; fg8 = bg8; fb = bb;
    }
    if (attr & TERM_ATTR_BOLD) {
        fr = (u8)MIN(255, (int)fr + 30);
        fg8 = (u8)MIN(255, (int)fg8 + 30);
        fb = (u8)MIN(255, (int)fb + 30);
    }
    if (attr & TERM_ATTR_FAINT) {
        fr = (u8)((int)fr * 3 / 5);
        fg8 = (u8)((int)fg8 * 3 / 5);
        fb = (u8)((int)fb * 3 / 5);
    }
    attr &= (u8)(TERM_ATTR_UNDERLINE | TERM_ATTR_STRUCK);
    *fg = ((u32)fr << 24) | ((u32)fg8 << 16) | ((u32)fb << 8) | attr;
    *bg = ((u32)br << 24) | ((u32)bg8 << 16) | ((u32)bb << 8);
}

void
renderer_upload_atlas(Renderer *r)
{
    size_t bytes;

    if (!r || !r->gpu || !r->atlas.atlas || !r->atlas_dirty)
        return;
    bytes = (size_t)r->atlas.slot_width * r->atlas.cols
        * (size_t)r->atlas.cell_height * r->atlas.rows * 4u;
    rend_texture_copy_data(r->gpu, &r->atlas_tex, r->atlas.atlas, bytes);
    r->atlas_dirty = 0;
}

Renderer *
renderer_create(RendererParams args)
{
    Renderer *r;
    u32 hash_n;
    u32 tiles;
    u32 reserved;
    u32 entries;
    GlyphTableParams table_params;
    size_t atlas_bytes;
    u32 cp;
    char share[256];

    if (!args.atlas_cols || !args.atlas_rows)
        return 0;
    hash_n = args.glyph_map_n ? args.glyph_map_n : 2048;
    if (hash_n & (hash_n - 1u))
        return 0;
    r = calloc(1, sizeof *r);
    if (!r)
        return 0;
    if (!peak_init()) {
        free(r);
        return 0;
    }
    if (!glyph_generator_set_font(&r->generator, font_path, (float)font_size_px)) {
        if (snprintf(share, sizeof share, "/usr/share/vt/%s", font_path) < 0
                || !glyph_generator_set_font(&r->generator, share, (float)font_size_px)) {
            renderer_destroy(r);
            return 0;
        }
    }
    glyph_generator_set_fallback(&r->generator, font_fallback_path);
    glyph_generator_set_emoji(&r->generator, font_emoji_path);
    r->atlas.cell_width = r->generator.cell_w;
    r->atlas.cell_height = r->generator.cell_h;
    r->atlas.slot_width = r->generator.cell_w * 2u;
    r->atlas.cols = args.atlas_cols;
    r->atlas.rows = args.atlas_rows;
    atlas_bytes = (size_t)r->atlas.slot_width * r->atlas.cols
        * (size_t)r->atlas.cell_height * r->atlas.rows * 4u;
    r->atlas.atlas = calloc(1, atlas_bytes);
    if (!r->atlas.atlas) {
        renderer_destroy(r);
        return 0;
    }
    tiles = args.atlas_cols * args.atlas_rows;
    reserved = GLYPH_DIRECT_N;
    if (tiles <= reserved) {
        renderer_destroy(r);
        return 0;
    }
    entries = args.glyph_n;
    if (!entries || entries + reserved > tiles)
        entries = tiles - reserved;
    if (entries < 2) {
        renderer_destroy(r);
        return 0;
    }
    memset(&table_params, 0, sizeof table_params);
    table_params.hash_count = hash_n;
    table_params.entry_count = entries;
    table_params.reserved_tile_count = reserved;
    table_params.cache_tile_count = args.atlas_cols;
    r->memory = malloc(glyph_table_size(table_params));
    r->glyph_table = glyph_table_place_in_memory(table_params, r->memory);
    if (!r->glyph_table) {
        renderer_destroy(r);
        return 0;
    }
    for (cp = GLYPH_DIRECT_LO; cp <= GLYPH_DIRECT_HI; cp++)
        glyph_generator_rasterize_codepoint(&r->generator, &r->atlas, cp - GLYPH_DIRECT_LO, cp);
    glyph_generator_rasterize_codepoint(&r->generator, &r->atlas, GLYPH_DIRECT_FFFD, 0xFFFDu);

    r->tile = 1;
    if (args.offscreen)
        return r;

    {
        uint32_t win_w;
        uint32_t win_h;
        RendBindingInfo bind;
        unsigned long vert_n;
        unsigned long frag_n;
        uint8_t *vert;
        uint8_t *frag;
        u32 tex_w;
        u32 tex_h;

        r->tile = 1;
        win_w = 80u * r->atlas.cell_width;
        win_h = 24u * r->atlas.cell_height;
        if (!win_w)
            win_w = 800;
        if (!win_h)
            win_h = 600;
        r->win = peak_window_open("vt", win_w, win_h, alpha < 1.f ? PEAK_WINDOW_TRANSPARENT : 0);
        if (!r->win.running) {
            renderer_destroy(r);
            return 0;
        }
        r->current_width = r->win.width ? r->win.width : win_w;
        r->current_height = r->win.height ? r->win.height : win_h;
        memset(&bind, 0, sizeof bind);
        bind.texture_bindings[0] = 0;
        bind.texture_array_sizes[0] = 1;
        bind.texture_binding_count = 1;
        bind.ssbo_bindings[0] = 1;
        bind.ssbo_array_sizes[0] = 1;
        bind.ssbo_binding_count = 1;
        r->gpu = rend_renderer_create(&r->win, REND_BACKEND_AUTO, NULL, vsync, &bind);
        if (!r->gpu) {
            renderer_destroy(r);
            return 0;
        }
        tex_w = r->atlas.slot_width * r->atlas.cols;
        tex_h = r->atlas.cell_height * r->atlas.rows;
        r->atlas_tex = rend_texture_create_from_data(r->gpu, r->atlas.atlas, tex_w, tex_h, REND_FORMAT_R8G8B8A8_UNORM);
        rend_descriptor_write_texture(r->gpu, &r->atlas_tex, 0, 0);
        vert_n = 0;
        frag_n = 0;
        vert = renderer_file_alloc("vulkan/vt.vert.spv", &vert_n);
        frag = renderer_file_alloc("vulkan/vt.frag.spv", &frag_n);
        r->tile = 0;
        r->pipeline = 0;
        {
            RendPushConstantInfo pc;

            memset(&pc, 0, sizeof pc);
            pc.size = sizeof (VtPushTile);
            if (vert && vert_n && frag && frag_n) {
                r->pipeline = rend_pipeline_create_graphics_spirv(
                    r->gpu, vert, vert_n, frag, frag_n,
                    NULL, 0, NULL, 0,
                    &pc, 1,
                    REND_POLYGON_MODE_FILL, REND_CULL_MODE_NONE, REND_TOPOLOGY_TRIANGLE_LIST,
                    REND_FORMAT_UNDEFINED, false);
                if (r->pipeline)
                    r->tile = 1;
            }
            if (!r->pipeline) {
                RendVertexBinding vbind;

                memset(&vbind, 0, sizeof vbind);
                vbind.binding = 0;
                vbind.stride = sizeof (VtInstance);
                vbind.input_rate = REND_INPUT_RATE_INSTANCE;
                pc.size = sizeof (VtPush);
                r->pipeline = rend_pipeline_create_graphics_c(
                    r->gpu, (void *)vt_cpu_vert, 0, (void *)vt_cpu_frag, 0,
                    &vbind, 1,
                    renderer_cell_attrs, 3,
                    &pc, 1,
                    REND_POLYGON_MODE_FILL, REND_CULL_MODE_NONE, REND_TOPOLOGY_TRIANGLE_STRIP,
                    REND_FORMAT_UNDEFINED, false);
            }
            if (r->pipeline && alpha < 1.f)
                rend_pipeline_set_blend(r->pipeline, true);
        }
        free(vert);
        free(frag);
        if (!r->pipeline) {
            VTFATAL("graphics pipeline");
            renderer_destroy(r);
            return 0;
        }
    }
    return r;
}

void
renderer_destroy(Renderer *r)
{
    if (!r)
        return;
    if (r->gpu) {
        rend_texture_destroy(r->gpu, &r->atlas_tex);
        if (r->instance_cap)
            rend_buffer_destroy(&r->instance);
        rend_quit();
        r->gpu = 0;
    }
    if (r->win.running)
        peak_window_close(&r->win);
    peak_quit();
    glyph_generator_destroy(&r->generator);
    free(r->atlas.atlas);
    free(r->memory);
    free(r);
}

int
renderer_frame_begin(Renderer *r, u32 bg)
{
    float red;
    float green;
    float blue;

    if (!r || !r->gpu || !r->pipeline)
        return 0;
    renderer_upload_atlas(r);
    if (!rend_renderer_frame_begin(r->gpu))
        return 0;
    red = (float)((bg >> 16) & 0xff) / 255.f;
    green = (float)((bg >> 8) & 0xff) / 255.f;
    blue = (float)(bg & 0xff) / 255.f;
    rend_cmd_render_begin(r->gpu, red, green, blue, alpha);
    rend_cmd_bind_pipeline(r->pipeline);
    r->clear_bg = bg;
    return 1;
}

void
renderer_flush(Renderer *r, u32 n)
{
    u32 fb_w;
    u32 fb_h;

    if (!r || !r->gpu || !r->pipeline)
        return;
    renderer_upload_atlas(r);
    if (r->tile) {
        VtPushTile tile;

        memset(&tile, 0, sizeof tile);
        tile.cell_w = r->atlas.cell_width;
        tile.cell_h = r->atlas.cell_height;
        tile.cols = r->instance_cols;
        tile.rows = r->instance_rows;
        tile.uv_x = r->atlas.cols ? 1.f / (float)r->atlas.cols : 0.f;
        tile.uv_y = r->atlas.rows ? 1.f / (float)r->atlas.rows : 0.f;
        tile.alpha = alpha;
        tile.def_bg = r->clear_bg << 8;
        rend_cmd_push_constants(r->pipeline, &tile, sizeof tile);
        rend_cmd_draw(r->pipeline, 3, 1);
        return;
    }
    if (!n)
        return;
    fb_w = r->current_width;
    fb_h = r->current_height;
    {
        VtPush pc;

        memset(&pc, 0, sizeof pc);
        pc.ndc_x = fb_w ? 2.f * (float)r->atlas.cell_width / (float)fb_w : 0.f;
        pc.ndc_y = fb_h ? 2.f * (float)r->atlas.cell_height / (float)fb_h : 0.f;
        pc.uv_x = r->atlas.cols ? 1.f / (float)r->atlas.cols : 0.f;
        pc.uv_y = r->atlas.rows ? 1.f / (float)r->atlas.rows : 0.f;
        pc.alpha = alpha;
        rend_cmd_bind_vertex_buffer(r->pipeline, 0, r->instance, 0);
        rend_cmd_push_constants(r->pipeline, &pc, sizeof pc);
        rend_cmd_draw(r->pipeline, 4, n);
    }
}

void
renderer_frame_end(Renderer *r)
{
    if (!r || !r->gpu)
        return;
    rend_cmd_render_end(r->gpu);
    rend_renderer_frame_end(r->gpu, NULL);
}

void
renderer_screenshot_term_ppm(Renderer *r, Term *term, const char *out_path)
{
    TermScreen *s;
    u32 cw;
    u32 ch;
    u32 atlas_w;
    u32 img_w;
    u32 img_h;
    u32 x;
    u32 y;
    u8 *img;
    u8 cr, cg, cb;
    u32 def_bg;
    u32 def_fg;
    FILE *f;

    if (!r || !term || !out_path || !r->atlas.atlas)
        return;
    s = term_screen(term);
    if (!s || !s->line || !s->cols || !s->rows)
        return;
    if (!r->atlas.cell_width || !r->atlas.slot_width || !r->atlas.cell_height)
        return;
    cw = r->atlas.cell_width;
    ch = r->atlas.cell_height;
    atlas_w = r->atlas.slot_width * r->atlas.cols;
    img_w = s->cols * cw;
    img_h = s->rows * ch;
    img = calloc((size_t)img_w * img_h * 3, 1);
    if (!img)
        return;
    def_bg = term->colors.bg[term->colors.bg_default < 8 ? term->colors.bg_default : 0] << 8;
    def_fg = term->colors.fg[term->colors.fg_default < 16 ? term->colors.fg_default : 7] << 8;
    renderer_unpack_rgb(def_bg, &cr, &cg, &cb);
    for (y = 0; y < img_h; y++) {
        u8 *row = img + (size_t)y * img_w * 3;
        for (x = 0; x < img_w; x++) {
            row[x * 3 + 0] = cr;
            row[x * 3 + 1] = cg;
            row[x * 3 + 2] = cb;
        }
    }
    for (y = 0; y < s->rows; y++) {
        int skip_wide_tail = 0;

        for (x = 0; x < s->cols; x++) {
            TermCell *cell;
            TermStyle st;
            u32 fg;
            u32 bg;
            codepoint_t cp;
            u32 idx;
            u32 ax;
            u32 ay;
            u32 py;
            u32 px;
            u32 cells;
            u8 fr, fg8, fb, br, bg8, bb;
            int color;
            int wide;
            int at_cursor;

            cell = term_cell_at(s, x, y);
            if (!cell)
                continue;
            at_cursor = !(term->mode & TERM_MODE_HIDE) && x == term->cursor.x && y == term->cursor.y;
            cp = cell->codepoint;
            st = term_cell_style(term, cell);
            fg = st.fg;
            bg = st.bg;
            if (!cp && !fg && !bg && !at_cursor) {
                skip_wide_tail = 0;
                continue;
            }
            if (!fg && !bg) {
                fg = def_fg;
                bg = def_bg;
            }
            if (at_cursor) {
                u32 tmp;

                tmp = fg;
                fg = bg;
                bg = tmp;
                if (!cp)
                    cp = (codepoint_t)' ';
            }
            if (cp == 0) {
                if (skip_wide_tail && !at_cursor) {
                    skip_wide_tail = 0;
                    continue;
                }
                cp = (codepoint_t)' ';
            }
            skip_wide_tail = 0;
            idx = renderer_glyph_get(r, cp);
            ax = (idx >> 16) & 31u;
            ay = idx & 0x7fu;
            renderer_bake_colors(&fg, &bg);
            renderer_unpack_rgb(fg, &fr, &fg8, &fb);
            renderer_unpack_rgb(bg, &br, &bg8, &bb);
            color = (idx & GLYPH_COLOR) != 0;
            wide = color && x + 1u < s->cols && term_cell_at(s, x + 1u, y)
                && term_cell_at(s, x + 1u, y)->codepoint == 0;
            cells = wide ? 2u : 1u;
            for (py = 0; py < ch; py++) {
                const u8 *src = r->atlas.atlas + ((ay * ch + py) * atlas_w + ax * r->atlas.slot_width) * 4u;
                u8 *dst = img + ((y * ch + py) * img_w + x * cw) * 3;
                int underline = (fg & TERM_ATTR_UNDERLINE) && py >= (ch * 86u / 100u);
                int struck = (fg & TERM_ATTR_STRUCK) && py + 1u >= ch / 2u && py <= ch / 2u + 1u;
                for (px = 0; px < cw * cells; px++) {
                    u32 cover;
                    u8 er, eg, eb;

                    if (color) {
                        er = src[px * 4u + 0];
                        eg = src[px * 4u + 1];
                        eb = src[px * 4u + 2];
                        cover = src[px * 4u + 3];
                    } else {
                        cover = src[px * 4u];
                        er = fr;
                        eg = fg8;
                        eb = fb;
                    }
                    if (underline || struck) {
                        cover = 255;
                        er = fr;
                        eg = fg8;
                        eb = fb;
                    }
                    dst[px * 3 + 0] = (u8)((br * (255 - cover) + er * cover) / 255);
                    dst[px * 3 + 1] = (u8)((bg8 * (255 - cover) + eg * cover) / 255);
                    dst[px * 3 + 2] = (u8)((bb * (255 - cover) + eb * cover) / 255);
                }
            }
            if (wide)
                skip_wide_tail = 1;
        }
    }
    f = fopen(out_path, "wb");
    if (!f) {
        free(img);
        return;
    }
    fprintf(f, "P6\n%u %u\n255\n", img_w, img_h);
    fwrite(img, 1, (size_t)img_w * img_h * 3, f);
    fclose(f);
    free(img);
}
