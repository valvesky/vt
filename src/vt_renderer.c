#pragma once
#include "config.h"
#include "vt.h"

typedef struct Atlas {
  unsigned char *atlas;
  uint32_t cell_width;
  uint32_t slot_width;
  uint32_t cell_height;
  uint32_t rows;
  uint32_t cols;
} Atlas;

struct Renderer {
#ifndef VT_HEADLESS
  RendRenderer gpu;
  RendPipeline pipeline;
  RendBuffer instance;
  RendTexture atlas_tex;
#endif
  uint32_t current_width;
  uint32_t current_height;
};

#ifndef VT_HEADLESS
static const RendVertexAttributes renderer_cell_attrs[] = {
  { .location = 0, .binding = 0, .offset = offsetof(VtInstance, pos), .format = REND_FORMAT_R32_UINT },
  { .location = 1, .binding = 0, .offset = offsetof(VtInstance, foreground), .format = REND_FORMAT_R32_UINT },
  { .location = 2, .binding = 0, .offset = offsetof(VtInstance, background), .format = REND_FORMAT_R32_UINT },
};
#endif

#ifndef VT_HEADLESS
static PeakWindow win;
static Renderer renderer;
#endif
static Atlas atlas;
static int ascent = 0;
static int descent = 0;
static int line_gap = 0;
static float scale = 0.0;
static stbtt_fontinfo glyph_font;
static unsigned char *glyph_ttf;
static stbtt_fontinfo glyph_fallback_font;
static unsigned char *glyph_fallback_ttf;
static float fallback_scale;
static int fallback_ascent;
static int fallback_ok;
static unsigned char *glyph_emoji_ttf;
static unsigned long glyph_emoji_n;
static const unsigned char *glyph_cmap;
static const unsigned char *glyph_cblc;
static const unsigned char *glyph_cbdt;
static unsigned long glyph_cmap_n;
static unsigned long glyph_cblc_n;
static unsigned long glyph_cbdt_n;
static int glyph_emoji_ok;
static bool glyph_atlas_dirty;
static VtLRU glyph_lru;
static vt_glyph_id glyph_ascii[128];

#ifdef DEBUG
enum {
  VT_STAGE_PARSE = 0,
  VT_STAGE_FILL,
  VT_STAGE_BEGIN,
  VT_STAGE_DRAW,
  VT_STAGE_END,
  VT_STAGE_PRESENT,
  VT_STAGE_N
};

static u64 vt_stage_ns[VT_STAGE_N];
static u32 vt_stage_n[VT_STAGE_N];

static void
vt_stage_add(int stage, u64 dt)
{
  vt_stage_ns[stage] += dt;
  vt_stage_n[stage]++;
}

static void
vt_stage_report(void)
{
  u64 avg[VT_STAGE_N];
  int i;

  for (i = 0; i < VT_STAGE_N; i++)
    avg[i] = vt_stage_n[i] ? vt_stage_ns[i] / vt_stage_n[i] : 0;
  VTINFO("avg parse %llu fill %llu begin %llu draw %llu end %llu present %llu ns (%u parse %u present)",
      (unsigned long long)avg[VT_STAGE_PARSE],
      (unsigned long long)avg[VT_STAGE_FILL],
      (unsigned long long)avg[VT_STAGE_BEGIN],
      (unsigned long long)avg[VT_STAGE_DRAW],
      (unsigned long long)avg[VT_STAGE_END],
      (unsigned long long)avg[VT_STAGE_PRESENT],
      vt_stage_n[VT_STAGE_PARSE],
      vt_stage_n[VT_STAGE_PRESENT]);
}
#endif

#ifndef VT_HEADLESS
static void vt_cpu_vert(RendCpuVarying *out, const RendCpuVertArgs *in);
static void vt_cpu_frag(float rgba[4], const RendCpuFragArgs *in);
#include "vulkan/vt.cpu.c"
static bool renderer_init(void);
static bool renderer_instance_make(RendBuffer *inst, u32 cols, u32 rows);
static void renderer_instance_release_prev(void);
static void renderer_get_grid(Renderer *r, u32 *cols, u32 *rows);
static void renderer_sync(Term *term, TermScreen *s, u32 cx, u32 cy, int cursor_on, color_packed_t cur_fg, color_packed_t cur_bg, int sel_on, u32 sel0, u32 sel1);
static void renderer_destroy(void);
#endif

static void *vt_file_alloc(const char *rel, unsigned long *n);
static bool glyph_table_init(const char *font_path, float pixel_height);
static void glyph_table_destroy(void);
static void renderer_unpack_rgb(color_packed_t packed, u8 *r, u8 *g, u8 *b);
static void renderer_bake_colors(color_packed_t *fg, color_packed_t *bg);
static bool renderer_screenshot_ppm(Term *term, TermScreen *s, u32 cur_x, u32 cur_y, color_packed_t cur_fg, color_packed_t cur_bg, const char *path);
static u16 glyph_be16(const unsigned char *p);
static u32 glyph_be32(const unsigned char *p);
static const unsigned char *glyph_ttf_table(const unsigned char *ttf, unsigned long n, const char *tag, unsigned long *len);
static u32 glyph_cmap_gid(const unsigned char *cmap, unsigned long n, codepoint_t cp);
static int glyph_emoji_png(codepoint_t cp, const unsigned char **png, unsigned long *png_n);
static void glyph_slot_clear(u32 slot);
static void glyph_blit_cover(u32 slot, const uint8_t *tmp, int cw, int ch);
static void glyph_blit_rgba_fit(u32 slot, const u8 *rgba, int w, int h, int cells);
static void glyph_rasterize_outline(stbtt_fontinfo *font, float sc, int asc, codepoint_t cp, u32 slot);
static int glyph_rasterize_emoji(codepoint_t cp, u32 slot);
static void glyph_rasterize_slot(codepoint_t cp, u32 slot);

static void *
vt_file_alloc(const char *rel, unsigned long *n)
{
  void *p;
  char buf[256];

#ifndef VT_HEADLESS
  p = peak_file_alloc(rel, n);
  if (p)
    return p;
  snprintf(buf, sizeof buf, "/usr/share/vt/%s", rel);
  return peak_file_alloc(buf, n);
#else
  {
    int pass;

    for (pass = 0; pass < 2; pass++) {
      const char *path;
      FILE *f;
      long sz;

      path = rel;
      if (pass == 1) {
        snprintf(buf, sizeof buf, "/usr/share/vt/%s", rel);
        path = buf;
      }
      f = fopen(path, "rb");
      if (!f)
        continue;
      if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        continue;
      }
      sz = ftell(f);
      if (sz < 0) {
        fclose(f);
        continue;
      }
      rewind(f);
      p = malloc((size_t)sz + 1);
      if (!p) {
        fclose(f);
        continue;
      }
      if (sz > 0 && fread(p, 1, (size_t)sz, f) != (size_t)sz) {
        free(p);
        fclose(f);
        continue;
      }
      fclose(f);
      ((unsigned char *)p)[sz] = 0;
      if (n)
        *n = (unsigned long)sz;
      return p;
    }
  }
  return NULL;
#endif
}

#ifndef VT_HEADLESS
static bool
renderer_init(void)
{
  RendBindingInfo bind_info = {0};
  RendPushConstantInfo pc;
  RendVertexBinding vbind;
  unsigned long vert_bytes = 0;
  unsigned long frag_bytes = 0;
  uint8_t *vert_spv;
  uint8_t *frag_spv;
  size_t atlas_w;
  size_t atlas_h;
  uint32_t bg;

  if (!peak_init()) {
    VTFATAL("peak_init");
    return false;
  }

  win = peak_window_open("vt", 800, 600, alpha < 1.f ? PEAK_WINDOW_TRANSPARENT : 0);
  if (!win.running) {
    VTFATAL("peak_window_open");
    return false;
  }

  renderer.current_width = win.width ? win.width : 800;
  renderer.current_height = win.height ? win.height : 600;

  bind_info.texture_bindings[0] = 0;
  bind_info.texture_array_sizes[0] = 1;
  bind_info.texture_binding_count = 1;

  renderer.gpu = rend_renderer_create(&win, REND_BACKEND_AUTO, NULL, vsync, &bind_info);
  if (!renderer.gpu) {
    VTFATAL("rend_renderer_create");
    return false;
  }
  VTINFO("vsync %d", vsync ? 1 : 0);

  if (!glyph_table_init(font_path, (float) font_size_px)) {
    return false;
  }

  atlas_w = (size_t)atlas.slot_width * atlas.cols;
  atlas_h = (size_t)atlas.cell_height * atlas.rows;
  renderer.atlas_tex = rend_texture_create_from_data(
      renderer.gpu, atlas.atlas, (uint32_t)atlas_w, (uint32_t)atlas_h, REND_FORMAT_R8G8B8A8_UNORM);
  if (!renderer.atlas_tex.handle) {
    VTFATAL("atlas tex");
    return false;
  }
  rend_descriptor_write_texture(renderer.gpu, &renderer.atlas_tex, 0, 0);
  glyph_atlas_dirty = false;

  pc.offset = 0;
  pc.size = sizeof(VtPush);
  vbind.binding = 0;
  vbind.stride = sizeof(VtInstance);
  vbind.input_rate = REND_INPUT_RATE_INSTANCE;
  renderer.pipeline = NULL;
  vert_spv = vt_file_alloc("vulkan/vt.vert.spv", &vert_bytes);
  frag_spv = vt_file_alloc("vulkan/vt.frag.spv", &frag_bytes);
  if (vert_spv && frag_spv) {
    renderer.pipeline = rend_pipeline_create_graphics_spirv(
        renderer.gpu,
        vert_spv, vert_bytes,
        frag_spv, frag_bytes,
        &vbind, 1,
        renderer_cell_attrs, LEN(renderer_cell_attrs),
        &pc, 1,
        REND_POLYGON_MODE_FILL,
        REND_CULL_MODE_NONE,
        REND_TOPOLOGY_TRIANGLE_STRIP,
        REND_FORMAT_UNDEFINED,
        false);
  }
  free(vert_spv);
  free(frag_spv);
  if (!renderer.pipeline) {
    renderer.pipeline = rend_pipeline_create_graphics_c(
        renderer.gpu,
        (void *)vt_cpu_vert, 0,
        (void *)vt_cpu_frag, 0,
        &vbind, 1,
        renderer_cell_attrs, LEN(renderer_cell_attrs),
        &pc, 1,
        REND_POLYGON_MODE_FILL,
        REND_CULL_MODE_NONE,
        REND_TOPOLOGY_TRIANGLE_STRIP,
        REND_FORMAT_UNDEFINED,
        false);
  }
  if (!renderer.pipeline) {
    VTFATAL("graphics pipeline");
    return false;
  }

  bg = ansi_bg[bg_color];
  VTINFO("Peak+Rend atlas %ux%u cell %ux%u bg %06x",
      (unsigned)atlas_w, (unsigned)atlas_h,
      atlas.cell_width, atlas.cell_height, bg);
  return true;
}
#endif

#ifndef VT_HEADLESS
static bool
renderer_instance_make(RendBuffer *inst, u32 cols, u32 rows)
{
  size_t ibytes;

  if (!cols || !rows || !renderer.gpu)
    return false;
  ibytes = (size_t)cols * (size_t)rows * sizeof(VtInstance);
  *inst = rend_buffer_create(renderer.gpu, ibytes, REND_BUFFER_VERTEX, false);
  return rend_buffer_mapped(inst) != NULL;
}

static RendBuffer inst_prev;

static void
renderer_instance_release_prev(void)
{
  if (inst_prev.handle)
    rend_buffer_destroy(&inst_prev);
  memset(&inst_prev, 0, sizeof inst_prev);
}

static void
renderer_get_grid(Renderer *r, u32 *cols, u32 *rows)
{
  *cols = atlas.cell_width ? r->current_width / atlas.cell_width : 0;
  *rows = atlas.cell_height ? r->current_height / atlas.cell_height : 0;
  if (!*cols)
    *cols = 80;
  if (!*rows)
    *rows = 24;
}

static void
renderer_sync(Term *term, TermScreen *s, u32 cx, u32 cy, int cursor_on,
    color_packed_t cur_fg, color_packed_t cur_bg, int sel_on, u32 sel0, u32 sel1)
{
  VtPush pc;
  VtInstance *inst;
  uint32_t bg;
  float r, g, b;
  u32 fb_w;
  u32 fb_h;
  u32 n;
  u32 x;
  u32 y;
  u32 cols;
  u32 rows;
  TermCell *cells;
#ifdef DEBUG
  u64 t0;
  u64 t_fill;
  u64 t_begin;
  u64 t_draw;
  u64 t_end;
#endif

  if (!renderer.gpu || !renderer.pipeline || !s || !s->cols || !s->rows)
    return;
  inst = rend_buffer_mapped(&renderer.instance);
  if (!inst)
    return;
#ifdef DEBUG
  t0 = peak_get_time();
#endif
  n = 0;
  cols = s->cols;
  rows = s->rows;
  cells = s->cell_buffer;
  for (y = 0; y < rows; y++) {
    TermCell *row;
    int skip_wide_tail;

    row = cells + (size_t)y * cols;
    skip_wide_tail = 0;
    for (x = 0; x < cols; x++) {
      TermCell *cell;
      TermStyle st;
      int at_cursor;
      int selected;
      int wide;
      codepoint_t cp;
      color_packed_t fg;
      color_packed_t bg;
      vt_glyph_id g;

      cell = &row[x];
      at_cursor = cursor_on && y == cy && x == cx;
      cp = cell->codepoint;
      st = term_cell_style(term, cell);
      fg = st.fg;
      bg = st.bg;
      selected = sel_on && (y * cols + x) >= sel0 && (y * cols + x) <= sel1;
      if (!cp && !fg && !bg && !at_cursor && !selected)
        continue;
      if (selected && !at_cursor) {
        color_packed_t t;

        t = fg;
        fg = bg;
        bg = t;
        if (!cp)
          cp = (codepoint_t)' ';
      }
      if (at_cursor) {
        if (cp) {
          fg = st.bg;
          bg = st.fg;
        } else {
          cp = (codepoint_t)' ';
          fg = cur_bg;
          bg = cur_fg;
        }
      }
      if (!cp) {
        if (skip_wide_tail && !at_cursor && !selected) {
          skip_wide_tail = 0;
          continue;
        }
        cp = (codepoint_t)' ';
      }
      skip_wide_tail = 0;
      if (cp == (codepoint_t)' '
          && !at_cursor && !selected
          && !(fg & (TERM_ATTR_UNDERLINE | TERM_ATTR_STRUCK | TERM_ATTR_REVERSE))
          && (bg & 0xffffff00u) == ((color_packed_t)ansi_bg[bg_color] << 8))
        continue;
      if (fg & (TERM_ATTR_REVERSE | TERM_ATTR_INVISIBLE | TERM_ATTR_BOLD | TERM_ATTR_FAINT))
        renderer_bake_colors(&fg, &bg);
      if (cp < 128)
        g = glyph_ascii[cp];
      else {
        g = vt_lru_peek(&glyph_lru, cp);
        if (g == VT_LRU_NONE)
          g = vt_glyph_get(cp);
      }
      wide = (g & VT_GLYPH_COLOR) && x + 1u < cols && row[x + 1u].codepoint == 0;
      inst[n].pos = (x << 16) | y;
      inst[n].foreground = (fg & 0xffffff00u)
        | (((g >> 16) & 31u) << 2)
        | ((g & VT_GLYPH_COLOR) ? 0x80u : 0)
        | ((fg >> 3) & 1u) | ((fg >> 6) & 2u);
      inst[n].background = (bg & 0xffffff00u) | (g & 0x7fu) | (wide ? 0x80u : 0);
      n++;
      if (wide)
        skip_wide_tail = 1;
    }
  }
  if (glyph_atlas_dirty && renderer.gpu && atlas.atlas && renderer.atlas_tex.handle) {
    size_t bytes;

    bytes = (size_t)atlas.slot_width * atlas.cols * (size_t)atlas.cell_height * atlas.rows * 4u;
    rend_texture_copy_data(renderer.gpu, &renderer.atlas_tex, atlas.atlas, bytes);
    glyph_atlas_dirty = false;
  }
#ifdef DEBUG
  t_fill = peak_get_time();
#endif
  if (!rend_renderer_frame_begin(renderer.gpu))
    return;
#ifdef DEBUG
  t_begin = peak_get_time();
#endif
  fb_w = renderer.current_width;
  fb_h = renderer.current_height;
  {
    RendTexture *color;

    color = rend_renderer_color_target(renderer.gpu);
    if (color) {
      if (rend_texture_width(color))
        fb_w = rend_texture_width(color);
      if (rend_texture_height(color))
        fb_h = rend_texture_height(color);
    }
  }

  memset(&pc, 0, sizeof pc);
  pc.ndc_x = fb_w ? 2.f * (float)atlas.cell_width / (float)fb_w : 0.f;
  pc.ndc_y = fb_h ? 2.f * (float)atlas.cell_height / (float)fb_h : 0.f;
  pc.uv_x = atlas.cols ? 1.f / (float)atlas.cols : 0.f;
  pc.uv_y = atlas.rows ? 1.f / (float)atlas.rows : 0.f;
  pc.alpha = alpha;

  bg = ansi_bg[bg_color];
  r = (float)((bg >> 16) & 0xFF) / 255.0f;
  g = (float)((bg >> 8) & 0xFF) / 255.0f;
  b = (float)(bg & 0xFF) / 255.0f;
  rend_cmd_render_begin(renderer.gpu, r, g, b, alpha);
  if (n) {
    rend_cmd_bind_pipeline(renderer.pipeline);
    rend_cmd_bind_vertex_buffer(renderer.pipeline, 0, renderer.instance, 0);
    rend_cmd_push_constants(renderer.pipeline, &pc, sizeof pc);
    rend_cmd_draw(renderer.pipeline, 4, n);
  }
  rend_cmd_render_end(renderer.gpu);
#ifdef DEBUG
  t_draw = peak_get_time();
#endif
  rend_renderer_frame_end(renderer.gpu, NULL);
#ifdef DEBUG
  t_end = peak_get_time();
  VTDEBUG("present fill %llu begin %llu draw %llu end %llu ns",
      (unsigned long long)(t_fill - t0),
      (unsigned long long)(t_begin - t_fill),
      (unsigned long long)(t_draw - t_begin),
      (unsigned long long)(t_end - t_draw));
  vt_stage_add(VT_STAGE_FILL, t_fill - t0);
  vt_stage_add(VT_STAGE_BEGIN, t_begin - t_fill);
  vt_stage_add(VT_STAGE_DRAW, t_draw - t_begin);
  vt_stage_add(VT_STAGE_END, t_end - t_draw);
#endif
}

static void
renderer_destroy(void)
{
  glyph_table_destroy();
  renderer_instance_release_prev();
  if (renderer.gpu) {
    rend_quit();
    renderer.gpu = NULL;
  }
  if (win.running)
    peak_window_close(&win);
  peak_quit();
  memset(&renderer, 0, sizeof renderer);
}
#endif

static void
renderer_unpack_rgb(color_packed_t packed, u8 *r, u8 *g, u8 *b)
{
  *r = (u8)((packed >> 24) & 0xff);
  *g = (u8)((packed >> 16) & 0xff);
  *b = (u8)((packed >> 8) & 0xff);
}

static void
renderer_bake_colors(color_packed_t *fg, color_packed_t *bg)
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
  *fg = ((color_packed_t)fr << 24) | ((color_packed_t)fg8 << 16)
      | ((color_packed_t)fb << 8) | attr;
  *bg = ((color_packed_t)br << 24) | ((color_packed_t)bg8 << 16)
      | ((color_packed_t)bb << 8);
}


static bool
renderer_screenshot_ppm(Term *term, TermScreen *s, u32 cur_x, u32 cur_y, color_packed_t cur_fg, color_packed_t cur_bg, const char *path)
{
  u32 cw;
  u32 ch;
  u32 atlas_w;
  u32 img_w;
  u32 img_h;
  u32 x;
  u32 y;
  u8 *img;
  u8 cr, cg, cb;
  FILE *f;

  if (!s || !path || !atlas.atlas || !s->cell_buffer)
    return false;
  if (!s->cols || !s->rows || !atlas.cell_width || !atlas.slot_width || !atlas.cell_height)
    return false;

  cw = atlas.cell_width;
  ch = atlas.cell_height;
  atlas_w = atlas.slot_width * atlas.cols;
  img_w = s->cols * cw;
  img_h = s->rows * ch;
  img = calloc((size_t)img_w * img_h * 3, 1);
  if (!img)
    return false;

  renderer_unpack_rgb(ansi_bg[bg_color] << 8, &cr, &cg, &cb);
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
      color_packed_t fg;
      color_packed_t bg;
      codepoint_t cp;
      vt_glyph_id idx;
      u32 ax;
      u32 ay;
      u32 py;
      u32 px;
      u32 cells;
      u8 fr, fg8, fb, br, bg8, bb;
      bool at_cursor;
      int color;
      int wide;

      cell = &s->cell_buffer[y * s->cols + x];
      at_cursor = (x == cur_x && y == cur_y);
      cp = cell->codepoint;
      st = term_cell_style(term, cell);
      fg = st.fg;
      bg = st.bg;
      if (at_cursor) {
        if (cell->codepoint) {
          cp = cell->codepoint;
          fg = st.bg;
          bg = st.fg;
        } else {
          cp = (codepoint_t)' ';
          fg = cur_bg;
          bg = cur_fg;
        }
      }

      if (!cp && !fg && !bg && !at_cursor) {
        skip_wide_tail = 0;
        continue;
      }
      if (cp == 0) {
        if (skip_wide_tail && !at_cursor) {
          skip_wide_tail = 0;
          continue;
        }
        cp = (codepoint_t)' ';
      }
      skip_wide_tail = 0;

      idx = vt_glyph_get(cp);
      ax = (idx >> 16) & 31u;
      ay = idx & 0x7fu;
      renderer_bake_colors(&fg, &bg);
      renderer_unpack_rgb(fg, &fr, &fg8, &fb);
      renderer_unpack_rgb(bg, &br, &bg8, &bb);
      color = (idx & VT_GLYPH_COLOR) != 0;
      wide = color && x + 1u < s->cols && s->cell_buffer[y * s->cols + x + 1u].codepoint == 0;
      cells = wide ? 2u : 1u;
      for (py = 0; py < ch; py++) {
        const u8 *src = atlas.atlas + ((ay * ch + py) * atlas_w + ax * atlas.slot_width) * 4u;
        u8 *dst = img + ((y * ch + py) * img_w + x * cw) * 3;
        int underline = (fg & TERM_ATTR_UNDERLINE) && py >= (ch * 86u / 100u);
        int struck = (fg & TERM_ATTR_STRUCK) && py + 1u >= ch / 2u && py <= ch / 2u + 1u;
        for (px = 0; px < cw * cells; px++) {
          u32 cover;
          u32 apx;
          u8 er, eg, eb;

          apx = px;
          if (color) {
            er = src[apx * 4u + 0];
            eg = src[apx * 4u + 1];
            eb = src[apx * 4u + 2];
            cover = src[apx * 4u + 3];
          } else {
            cover = src[apx * 4u];
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

  f = fopen(path, "wb");
  if (!f) {
    VTERROR("fopen");
    free(img);
    return false;
  }
  fprintf(f, "P6\n%u %u\n255\n", img_w, img_h);
  fwrite(img, 1, (size_t)img_w * img_h * 3, f);
  fclose(f);
  free(img);
  return true;
}


#define VT_BOX(n, e, s, w) ((u8)(((n) << 6) | ((e) << 4) | ((s) << 2) | (w)))
static const u8 vt_box_arm[0x80] = {
  [0x00] = VT_BOX(0, 1, 0, 1), [0x01] = VT_BOX(0, 2, 0, 2),
  [0x02] = VT_BOX(1, 0, 1, 0), [0x03] = VT_BOX(2, 0, 2, 0),
  [0x0C] = VT_BOX(0, 1, 1, 0), [0x0F] = VT_BOX(0, 2, 2, 0),
  [0x10] = VT_BOX(0, 0, 1, 1), [0x13] = VT_BOX(0, 0, 2, 2),
  [0x14] = VT_BOX(1, 1, 0, 0), [0x17] = VT_BOX(2, 2, 0, 0),
  [0x18] = VT_BOX(1, 0, 0, 1), [0x1B] = VT_BOX(2, 0, 0, 2),
  [0x1C] = VT_BOX(1, 1, 1, 0), [0x20] = VT_BOX(2, 1, 2, 0),
  [0x23] = VT_BOX(2, 2, 2, 0), [0x24] = VT_BOX(1, 0, 1, 1),
  [0x28] = VT_BOX(2, 0, 2, 1), [0x2B] = VT_BOX(2, 0, 2, 2),
  [0x2C] = VT_BOX(0, 1, 1, 1), [0x2F] = VT_BOX(0, 2, 1, 2),
  [0x33] = VT_BOX(0, 2, 2, 2), [0x34] = VT_BOX(1, 1, 0, 1),
  [0x37] = VT_BOX(1, 2, 0, 2), [0x3B] = VT_BOX(2, 2, 0, 2),
  [0x3C] = VT_BOX(1, 1, 1, 1), [0x4B] = VT_BOX(2, 2, 2, 2),
  [0x50] = VT_BOX(0, 2, 0, 2), [0x51] = VT_BOX(2, 0, 2, 0),
  [0x54] = VT_BOX(0, 2, 2, 0), [0x57] = VT_BOX(0, 0, 2, 2),
  [0x5A] = VT_BOX(2, 2, 0, 0), [0x5D] = VT_BOX(2, 0, 0, 2),
  [0x60] = VT_BOX(2, 2, 2, 0), [0x63] = VT_BOX(2, 0, 2, 2),
  [0x66] = VT_BOX(0, 2, 2, 2), [0x69] = VT_BOX(2, 2, 0, 2),
  [0x6C] = VT_BOX(2, 2, 2, 2), [0x6D] = VT_BOX(0, 1, 1, 0),
  [0x6E] = VT_BOX(0, 0, 1, 1), [0x6F] = VT_BOX(1, 0, 0, 1),
  [0x70] = VT_BOX(1, 1, 0, 0), [0x74] = VT_BOX(0, 0, 0, 1),
  [0x75] = VT_BOX(1, 0, 0, 0), [0x76] = VT_BOX(0, 1, 0, 0),
  [0x77] = VT_BOX(0, 0, 1, 0), [0x78] = VT_BOX(0, 0, 0, 2),
  [0x79] = VT_BOX(2, 0, 0, 0), [0x7A] = VT_BOX(0, 2, 0, 0),
  [0x7B] = VT_BOX(0, 0, 2, 0),
};

static u16
glyph_be16(const unsigned char *p)
{
  return (u16)(((u16)p[0] << 8) | p[1]);
}

static u32
glyph_be32(const unsigned char *p)
{
  return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static const unsigned char *
glyph_ttf_table(const unsigned char *ttf, unsigned long n, const char *tag, unsigned long *len)
{
  u16 num;
  u16 i;

  if (!ttf || n < 12 || !tag || !len)
    return NULL;
  num = glyph_be16(ttf + 4);
  if (n < 12u + (unsigned long)num * 16u)
    return NULL;
  for (i = 0; i < num; i++) {
    const unsigned char *rec = ttf + 12 + (unsigned long)i * 16u;
    u32 off;
    u32 size;

    if (memcmp(rec, tag, 4) != 0)
      continue;
    off = glyph_be32(rec + 8);
    size = glyph_be32(rec + 12);
    if ((unsigned long)off + size > n)
      return NULL;
    *len = size;
    return ttf + off;
  }
  return NULL;
}

static u32
glyph_cmap_gid(const unsigned char *cmap, unsigned long n, codepoint_t cp)
{
  u16 nrec;
  u16 i;

  if (!cmap || n < 4)
    return 0;
  nrec = glyph_be16(cmap + 2);
  for (i = 0; i < nrec; i++) {
    u32 ro;
    u16 fmt;

    if (4u + (unsigned long)i * 8u + 8u > n)
      return 0;
    ro = glyph_be32(cmap + 4 + (unsigned long)i * 8u + 4);
    if ((unsigned long)ro + 2u > n)
      continue;
    fmt = glyph_be16(cmap + ro);
    if (fmt != 12)
      continue;
    if ((unsigned long)ro + 16u > n)
      continue;
    {
      u32 ngrp = glyph_be32(cmap + ro + 12);
      u32 g;

      for (g = 0; g < ngrp; g++) {
        const unsigned char *gr = cmap + ro + 16 + g * 12u;
        u32 s;
        u32 e;
        u32 start;

        if ((unsigned long)ro + 16u + (g + 1u) * 12u > n)
          break;
        s = glyph_be32(gr);
        e = glyph_be32(gr + 4);
        start = glyph_be32(gr + 8);
        if (cp >= s && cp <= e)
          return start + (cp - s);
      }
    }
  }
  return 0;
}

static int
glyph_emoji_png(codepoint_t cp, const unsigned char **png, unsigned long *png_n)
{
  u32 gid;
  u32 num;
  u32 i;

  if (!glyph_emoji_ok || !png || !png_n)
    return 0;
  gid = glyph_cmap_gid(glyph_cmap, glyph_cmap_n, cp);
  if (!gid)
    return 0;
  if (glyph_cblc_n < 8)
    return 0;
  num = glyph_be32(glyph_cblc + 4);
  for (i = 0; i < num; i++) {
    const unsigned char *b;
    u32 array_off;
    u32 nindex;
    u32 j;

    if (8u + (i + 1u) * 48u > glyph_cblc_n)
      return 0;
    b = glyph_cblc + 8 + i * 48u;
    array_off = glyph_be32(b);
    nindex = glyph_be32(b + 8);
    for (j = 0; j < nindex; j++) {
      const unsigned char *rec;
      const unsigned char *st;
      u16 first;
      u16 last;
      u32 add_off;
      u16 index_format;
      u16 image_format;
      u32 image_off;

      if (array_off + (j + 1u) * 8u > glyph_cblc_n)
        break;
      rec = glyph_cblc + array_off + j * 8u;
      first = glyph_be16(rec);
      last = glyph_be16(rec + 2);
      add_off = glyph_be32(rec + 4);
      if (gid < first || gid > last)
        continue;
      st = glyph_cblc + array_off + add_off;
      if (st + 8 > glyph_cblc + glyph_cblc_n)
        return 0;
      index_format = glyph_be16(st);
      image_format = glyph_be16(st + 2);
      image_off = glyph_be32(st + 4);
      if (image_format != 17 || index_format != 1)
        return 0;
      {
        const unsigned char *oa = st + 8 + (u32)(gid - first) * 4u;
        const unsigned char *p;
        u32 off;
        u32 dlen;

        if (oa + 4 > glyph_cblc + glyph_cblc_n)
          return 0;
        off = glyph_be32(oa);
        p = glyph_cbdt + image_off + off;
        if (p + 9 > glyph_cbdt + glyph_cbdt_n)
          return 0;
        dlen = glyph_be32(p + 5);
        if (p + 9 + dlen > glyph_cbdt + glyph_cbdt_n)
          return 0;
        *png = p + 9;
        *png_n = dlen;
        return 1;
      }
    }
  }
  return 0;
}

static void
glyph_slot_clear(u32 slot)
{
  int cw;
  int ch;
  int atlas_w;
  int cell_x;
  int cell_y;
  int stride;
  int row;

  if (!atlas.atlas)
    return;
  cw = (int)atlas.slot_width;
  ch = (int)atlas.cell_height;
  atlas_w = cw * (int)atlas.cols;
  cell_x = (int)(slot % atlas.cols) * cw;
  cell_y = (int)(slot / atlas.cols) * ch;
  stride = atlas_w * 4;
  for (row = 0; row < ch; row++)
    memset(atlas.atlas + (cell_y + row) * stride + cell_x * 4, 0, (size_t)cw * 4u);
}

static void
glyph_blit_cover(u32 slot, const uint8_t *tmp, int cw, int ch)
{
  int atlas_w;
  int cell_x;
  int cell_y;
  int stride;
  int row;
  int col;

  atlas_w = (int)atlas.slot_width * (int)atlas.cols;
  cell_x = (int)(slot % atlas.cols) * (int)atlas.slot_width;
  cell_y = (int)(slot / atlas.cols) * ch;
  stride = atlas_w * 4;
  glyph_slot_clear(slot);
  for (row = 0; row < ch; row++) {
    const uint8_t *src = tmp + (size_t)row * (size_t)cw;
    u8 *dst = atlas.atlas + (cell_y + row) * stride + cell_x * 4;

    for (col = 0; col < cw; col++) {
      dst[col * 4 + 0] = src[col];
      dst[col * 4 + 3] = src[col];
    }
  }
  glyph_atlas_dirty = true;
}

static void
glyph_blit_rgba_fit(u32 slot, const u8 *rgba, int w, int h, int cells)
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

  if (!atlas.atlas || !rgba || w <= 0 || h <= 0)
    return;
  if (cells < 1)
    cells = 1;
  sw = (int)atlas.slot_width;
  ch = (int)atlas.cell_height;
  tw = (int)atlas.cell_width * cells;
  if (tw > sw)
    tw = sw;
  glyph_slot_clear(slot);
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
  atlas_w = sw * (int)atlas.cols;
  cell_x = (int)(slot % atlas.cols) * sw;
  cell_y = (int)(slot / atlas.cols) * ch;
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
      dst = atlas.atlas + (cell_y + oy + y) * stride + (cell_x + ox + x) * 4;
      dst[0] = (u8)(r / n);
      dst[1] = (u8)(g / n);
      dst[2] = (u8)(b / n);
      dst[3] = (u8)(a / n);
    }
  }
  glyph_atlas_dirty = true;
}

static void
glyph_rasterize_outline(stbtt_fontinfo *font, float sc, int asc, codepoint_t cp, u32 slot)
{
  int x0, y0, x1, y1;
  int bw, bh;
  uint8_t *bitmap;
  int cell_x;
  int cell_y;
  int atlas_width_px;
  int dst_cell_x_px;
  int dst_cell_y_px;
  int ascent_px;
  int row, col;
  int stride;

  glyph_slot_clear(slot);
  stbtt_GetCodepointBitmapBox(font, (int)cp, sc, sc, &x0, &y0, &x1, &y1);
  bw = x1 - x0;
  bh = y1 - y0;
  if (bw <= 0 || bh <= 0)
    return;
  bitmap = malloc((size_t)bw * (size_t)bh);
  if (!bitmap)
    return;
  stbtt_MakeCodepointBitmap(font, bitmap, bw, bh, bw, sc, sc, (int)cp);
  cell_x = (int)(slot % atlas.cols);
  cell_y = (int)(slot / atlas.cols);
  atlas_width_px = (int)atlas.slot_width * (int)atlas.cols;
  stride = atlas_width_px * 4;
  dst_cell_x_px = cell_x * (int)atlas.slot_width;
  dst_cell_y_px = cell_y * (int)atlas.cell_height;
  ascent_px = (int)(sc * (float)asc);
  for (row = 0; row < bh; row++) {
    for (col = 0; col < bw; col++) {
      int dst_x = dst_cell_x_px + col + x0;
      int dst_y = dst_cell_y_px + ascent_px + y0 + row;
      u8 cover;
      u8 *dst;

      if (dst_x < dst_cell_x_px || dst_x >= dst_cell_x_px + (int)atlas.cell_width ||
          dst_y < dst_cell_y_px || dst_y >= dst_cell_y_px + (int)atlas.cell_height)
        continue;
      cover = bitmap[row * bw + col];
      dst = atlas.atlas + dst_y * stride + dst_x * 4;
      dst[0] = cover;
      dst[3] = cover;
    }
  }
  free(bitmap);
  glyph_atlas_dirty = true;
}

static int
glyph_rasterize_emoji(codepoint_t cp, u32 slot)
{
  const unsigned char *png;
  unsigned long png_n;
  u8 *rgba;
  int w;
  int h;
  int n;

  if (!glyph_emoji_png(cp, &png, &png_n))
    return 0;
  rgba = stbi_load_from_memory(png, (int)png_n, &w, &h, &n, 4);
  if (!rgba)
    return 0;
  glyph_blit_rgba_fit(slot, rgba, w, h, (cp >= 0x1F300 && cp <= 0x1FAFF) ? 2 : 1);
  stbi_image_free(rgba);
  return 1;
}

static void
glyph_fill_rect(uint8_t *tmp, int cw, int ch, int x0, int y0, int x1, int y1, uint8_t cover)
{
  int y;

  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 > cw)
    x1 = cw;
  if (y1 > ch)
    y1 = ch;
  if (x1 <= x0 || y1 <= y0)
    return;
  for (y = y0; y < y1; y++)
    memset(tmp + y * cw + x0, cover, (size_t)(x1 - x0));
}

static void
glyph_rasterize_slot(codepoint_t cp, u32 slot)
{
  if (cp >= 0x2500 && cp <= 0x259F) {
    int n, e, s, w;
    int cw, ch, mx, my, light, heavy;
    uint8_t *tmp;

    n = e = s = w = 0;
    if (cp < 0x2580) {
      u8 v;

      v = vt_box_arm[cp - 0x2500];
      n = v >> 6;
      e = (v >> 4) & 3;
      s = (v >> 2) & 3;
      w = v & 3;
      if (!(n | e | s | w))
        goto not_box;
    }
    cw = (int)atlas.cell_width;
    ch = (int)atlas.cell_height;
    mx = cw / 2;
    my = ch / 2;
    light = cw >= 16 ? 2 : 1;
    heavy = cw >= 16 ? 4 : 2;
    tmp = calloc((size_t)cw * (size_t)ch, 1);
    if (!tmp)
      return;
    if (cp >= 0x2580 && cp <= 0x259F) {
      int by0, by1, bx0, bx1;

      by0 = 0;
      by1 = ch;
      bx0 = 0;
      bx1 = cw;
      if (cp == 0x2580) {
        by0 = 0;
        by1 = ch / 2;
      } else if (cp == 0x2584) {
        by0 = ch / 2;
        by1 = ch;
      } else if (cp == 0x258C) {
        bx1 = cw / 2;
      } else if (cp == 0x2590) {
        bx0 = cw / 2;
      } else if (cp == 0x2591 || cp == 0x2592 || cp == 0x2593) {
        uint8_t cover = (cp == 0x2591) ? 64 : (cp == 0x2592) ? 128 : 192;
        glyph_fill_rect(tmp, cw, ch, 0, 0, cw, ch, cover);
        goto blit;
      }
      glyph_fill_rect(tmp, cw, ch, bx0, by0, bx1, by1, 255);
      goto blit;
    }
    if (w) {
      int thick = w == 2 ? heavy : light;

      glyph_fill_rect(tmp, cw, ch, 0, my - thick / 2, mx + thick / 2, my - thick / 2 + thick, 255);
    }
    if (e) {
      int thick = e == 2 ? heavy : light;

      glyph_fill_rect(tmp, cw, ch, mx - thick / 2, my - thick / 2, cw, my - thick / 2 + thick, 255);
    }
    if (n) {
      int thick = n == 2 ? heavy : light;

      glyph_fill_rect(tmp, cw, ch, mx - thick / 2, 0, mx - thick / 2 + thick, my + thick / 2, 255);
    }
    if (s) {
      int thick = s == 2 ? heavy : light;

      glyph_fill_rect(tmp, cw, ch, mx - thick / 2, my - thick / 2, mx - thick / 2 + thick, ch, 255);
    }
  blit:
    glyph_blit_cover(slot, tmp, cw, ch);
    free(tmp);
    return;
  }
not_box:
  glyph_rasterize_outline(&glyph_font, scale, ascent, cp, slot);
}

vt_glyph_id
vt_glyph_get(codepoint_t codepoint)
{
  vt_glyph_id packed;
  u32 slot;
  int gi;
  int from_fallback;
  int from_emoji;
  int color;
  int pin;
  const unsigned char *png;
  unsigned long png_n;

  if (codepoint < 128)
    return glyph_ascii[codepoint];

  packed = vt_lru_peek(&glyph_lru, codepoint);
  if (packed != VT_LRU_NONE) {
    slot = vt_lru_find(&glyph_lru, codepoint);
    if (slot != VT_LRU_NONE)
      vt_lru_touch(&glyph_lru, slot);
    return packed;
  }

  from_fallback = 0;
  from_emoji = 0;
  color = 0;
  if (!(codepoint >= 0x2500 && codepoint <= 0x259F)) {
    if (glyph_emoji_ok && glyph_emoji_png(codepoint, &png, &png_n))
      from_emoji = 1;
    else {
      gi = stbtt_FindGlyphIndex(&glyph_font, (int)codepoint);
      if (!gi && fallback_ok) {
        gi = stbtt_FindGlyphIndex(&glyph_fallback_font, (int)codepoint);
        if (gi)
          from_fallback = 1;
      }
      if (!gi && codepoint != UTF_INVALID) {
        u32 fffd;

        packed = vt_glyph_get(UTF_INVALID);
        fffd = vt_lru_find(&glyph_lru, UTF_INVALID);
        if (fffd != VT_LRU_NONE)
          vt_lru_alias(&glyph_lru, codepoint, fffd);
        return packed;
      }
      if (!gi && codepoint == UTF_INVALID)
        return vt_glyph_get((codepoint_t)'?');
    }
  }

  slot = vt_lru_alloc(&glyph_lru);
  if (slot == VT_LRU_NONE) {
    VTWARN("glyph atlas full");
    if (codepoint != UTF_INVALID)
      return vt_glyph_get(UTF_INVALID);
    return vt_lru_pack(0);
  }

  if (from_emoji) {
    if (!glyph_rasterize_emoji(codepoint, slot))
      glyph_rasterize_outline(&glyph_font, scale, ascent, UTF_INVALID, slot);
    else
      color = 1;
  } else if (from_fallback) {
    glyph_rasterize_outline(&glyph_fallback_font, fallback_scale, fallback_ascent, codepoint, slot);
  } else {
    glyph_rasterize_slot(codepoint, slot);
  }
  pin = (codepoint >= VT_GLYPH_PIN_LO && codepoint <= VT_GLYPH_PIN_HI) || codepoint == UTF_INVALID;
  vt_lru_put(&glyph_lru, codepoint, slot, pin, color);
  return vt_lru_pack(slot) | (color ? VT_GLYPH_COLOR : 0);
}

static bool
glyph_table_init(const char *path, float pixel_height)
{
  unsigned long ttf_size = 0;
  Atlas new_atlas = {0};
  size_t atlas_width_px;
  size_t atlas_height_px;
  int max_advance = 0;
  int i;

  glyph_ttf = vt_file_alloc(path, &ttf_size);
  if (!glyph_ttf) {
    VTERROR("peak_file_alloc font %s", path);
    return false;
  }
  if (!stbtt_InitFont(&glyph_font, glyph_ttf, 0)) {
    VTERROR("stbtt_InitFont");
    free(glyph_ttf);
    glyph_ttf = NULL;
    return false;
  }

  scale = stbtt_ScaleForPixelHeight(&glyph_font, pixel_height);
  stbtt_GetFontVMetrics(&glyph_font, &ascent, &descent, &line_gap);

  new_atlas.cell_height = (uint32_t) (scale * (float) (ascent - descent + line_gap));
  for (i = 32; i < 128; i++) {
    int ax;
    stbtt_GetCodepointHMetrics(&glyph_font, i, &ax, 0);
    if (ax > max_advance) max_advance = ax;
  }
  new_atlas.cell_width = (uint32_t) (scale * (float) max_advance);
  new_atlas.slot_width = new_atlas.cell_width * 2u;
  new_atlas.rows = VT_ATLAS_ROWS;
  new_atlas.cols = VT_ATLAS_COLS;

  atlas_width_px  = (size_t) new_atlas.slot_width  * new_atlas.cols;
  atlas_height_px = (size_t) new_atlas.cell_height * new_atlas.rows;

  new_atlas.atlas = calloc(atlas_width_px * atlas_height_px * 4u, 1);
  if (!new_atlas.atlas) {
    free(glyph_ttf);
    glyph_ttf = NULL;
    return false;
  }

  atlas = new_atlas;
  glyph_atlas_dirty = false;
  vt_lru_init(&glyph_lru);
  fallback_ok = 0;
  fallback_scale = 0.f;
  fallback_ascent = 0;
  glyph_emoji_ok = 0;
  glyph_cmap = NULL;
  glyph_cblc = NULL;
  glyph_cbdt = NULL;
  glyph_cmap_n = 0;
  glyph_cblc_n = 0;
  glyph_cbdt_n = 0;

  if (font_fallback_path[0]) {
    unsigned long n = 0;

    glyph_fallback_ttf = vt_file_alloc(font_fallback_path, &n);
    if (glyph_fallback_ttf && stbtt_InitFont(&glyph_fallback_font, glyph_fallback_ttf,
          stbtt_GetFontOffsetForIndex(glyph_fallback_ttf, 0))) {
      fallback_ok = 1;
      fallback_scale = stbtt_ScaleForPixelHeight(&glyph_fallback_font, pixel_height);
      stbtt_GetFontVMetrics(&glyph_fallback_font, &fallback_ascent, 0, 0);
    } else {
      free(glyph_fallback_ttf);
      glyph_fallback_ttf = NULL;
      if (n)
        VTWARN("fallback font %s", font_fallback_path);
    }
  }
  if (font_emoji_path[0]) {
    glyph_emoji_n = 0;
    glyph_emoji_ttf = vt_file_alloc(font_emoji_path, &glyph_emoji_n);
    if (glyph_emoji_ttf) {
      glyph_cmap = glyph_ttf_table(glyph_emoji_ttf, glyph_emoji_n, "cmap", &glyph_cmap_n);
      glyph_cblc = glyph_ttf_table(glyph_emoji_ttf, glyph_emoji_n, "CBLC", &glyph_cblc_n);
      glyph_cbdt = glyph_ttf_table(glyph_emoji_ttf, glyph_emoji_n, "CBDT", &glyph_cbdt_n);
      if (glyph_cmap && glyph_cblc && glyph_cbdt)
        glyph_emoji_ok = 1;
      else {
        glyph_cmap = NULL;
        glyph_cblc = NULL;
        glyph_cbdt = NULL;
        free(glyph_emoji_ttf);
        glyph_emoji_ttf = NULL;
        VTWARN("emoji font %s", font_emoji_path);
      }
    }
  }

  for (i = 32; i < 128; i++) {
    u32 slot = vt_lru_alloc(&glyph_lru);
    glyph_rasterize_slot((codepoint_t)i, slot);
    vt_lru_put(&glyph_lru, (codepoint_t)i, slot, 1, 0);
    glyph_ascii[i] = vt_lru_pack(slot);
  }
  if (stbtt_FindGlyphIndex(&glyph_font, (int)UTF_INVALID)) {
    u32 slot = vt_lru_alloc(&glyph_lru);
    glyph_rasterize_slot(UTF_INVALID, slot);
    vt_lru_put(&glyph_lru, UTF_INVALID, slot, 1, 0);
  }

  return true;
}

static void
glyph_table_destroy(void)
{
  if (atlas.atlas) {
    free(atlas.atlas);
    atlas.atlas = NULL;
  }
  if (glyph_ttf) {
    free(glyph_ttf);
    glyph_ttf = NULL;
  }
  if (glyph_fallback_ttf) {
    free(glyph_fallback_ttf);
    glyph_fallback_ttf = NULL;
  }
  fallback_ok = 0;
  if (glyph_emoji_ttf) {
    free(glyph_emoji_ttf);
    glyph_emoji_ttf = NULL;
  }
  glyph_emoji_ok = 0;
  glyph_cmap = NULL;
  glyph_cblc = NULL;
  glyph_cbdt = NULL;
}
