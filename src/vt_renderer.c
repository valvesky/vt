#pragma once
#include "config.h"
#include "vt.h"

typedef struct Atlas {
  unsigned char *atlas;
  uint32_t cell_width;
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

  atlas_w = (size_t)atlas.cell_width * atlas.cols;
  atlas_h = (size_t)atlas.cell_height * atlas.rows;
  renderer.atlas_tex = rend_texture_create_from_data(
      renderer.gpu, atlas.atlas, (uint32_t)atlas_w, (uint32_t)atlas_h, REND_FORMAT_R8_UNORM);
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

    row = cells + (size_t)y * cols;
    for (x = 0; x < cols; x++) {
      TermCell *cell;
      int at_cursor;
      int selected;
      codepoint_t cp;
      color_packed_t fg;
      color_packed_t bg;
      vt_glyph_id g;

      cell = &row[x];
      at_cursor = cursor_on && y == cy && x == cx;
      cp = cell->codepoint;
      fg = term_cell_fg(term, cell);
      bg = term_cell_bg(term, cell);
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
          fg = term_cell_bg(term, cell);
          bg = term_cell_fg(term, cell);
        } else {
          cp = (codepoint_t)' ';
          fg = cur_bg;
          bg = cur_fg;
        }
      }
      if (!cp)
        cp = (codepoint_t)' ';
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
      inst[n].pos = (x << 16) | y;
      inst[n].foreground = (fg & 0xffffff00u) | ((g >> 16) << 2) | ((fg >> 3) & 1u) | ((fg >> 6) & 2u);
      inst[n].background = (bg & 0xffffff00u) | (g & 0xffu);
      n++;
    }
  }
  if (glyph_atlas_dirty && renderer.gpu && atlas.atlas && renderer.atlas_tex.handle) {
    size_t bytes;

    bytes = (size_t)atlas.cell_width * atlas.cols * (size_t)atlas.cell_height * atlas.rows;
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
  if (!s->cols || !s->rows || !atlas.cell_width || !atlas.cell_height)
    return false;

  cw = atlas.cell_width;
  ch = atlas.cell_height;
  atlas_w = cw * atlas.cols;
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
    for (x = 0; x < s->cols; x++) {
      TermCell *cell;
      color_packed_t fg;
      color_packed_t bg;
      codepoint_t cp;
      vt_glyph_id idx;
      u32 ax;
      u32 ay;
      u32 py;
      u32 px;
      u8 fr, fg8, fb, br, bg8, bb;
      bool at_cursor;

      cell = &s->cell_buffer[y * s->cols + x];
      at_cursor = (x == cur_x && y == cur_y);
      cp = cell->codepoint;
      fg = term_cell_fg(term, cell);
      bg = term_cell_bg(term, cell);
      if (at_cursor) {
        if (cell->codepoint) {
          cp = cell->codepoint;
          fg = term_cell_bg(term, cell);
          bg = term_cell_fg(term, cell);
        } else {
          cp = (codepoint_t)' ';
          fg = cur_bg;
          bg = cur_fg;
        }
      }

      if (!cp && !fg && !bg && !at_cursor)
        continue;
      if (cp == 0)
        cp = (codepoint_t)' ';

      idx = vt_glyph_get(cp);
      ax = idx >> 16;
      ay = idx & 0xffff;
      renderer_bake_colors(&fg, &bg);
      renderer_unpack_rgb(fg, &fr, &fg8, &fb);
      renderer_unpack_rgb(bg, &br, &bg8, &bb);
      for (py = 0; py < ch; py++) {
        const u8 *src = atlas.atlas + (ay * ch + py) * atlas_w + ax * cw;
        u8 *dst = img + ((y * ch + py) * img_w + x * cw) * 3;
        int underline = (fg & TERM_ATTR_UNDERLINE) && py >= (ch * 86u / 100u);
        int struck = (fg & TERM_ATTR_STRUCK) && py + 1u >= ch / 2u && py <= ch / 2u + 1u;
        for (px = 0; px < cw; px++) {
          u32 cover = src[px];
          if (underline || struck)
            cover = 255;
          dst[px * 3 + 0] = (u8)((br * (255 - cover) + fr * cover) / 255);
          dst[px * 3 + 1] = (u8)((bg8 * (255 - cover) + fg8 * cover) / 255);
          dst[px * 3 + 2] = (u8)((bb * (255 - cover) + fb * cover) / 255);
        }
      }
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
  int x0, y0, x1, y1;
  int bw, bh;
  uint8_t *bitmap;

  if (cp >= 0x2500 && cp <= 0x259F) {
    int n, e, s, w;
    int cw, ch, atlas_w, cell_x, cell_y, mx, my, light, heavy, row;
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
    atlas_w = cw * (int)atlas.cols;
    cell_x = (int)(slot % atlas.cols) * cw;
    cell_y = (int)(slot / atlas.cols) * ch;
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
    for (row = 0; row < ch; row++) {
      memcpy(atlas.atlas + (size_t)(cell_y + row) * (size_t)atlas_w + (size_t)cell_x,
          tmp + (size_t)row * (size_t)cw, (size_t)cw);
    }
    free(tmp);
    glyph_atlas_dirty = true;
    return;
  }
not_box:
  stbtt_GetCodepointBitmapBox(&glyph_font, (int)cp, scale, scale, &x0, &y0, &x1, &y1);
  bw = x1 - x0;
  bh = y1 - y0;
  if (bw <= 0 || bh <= 0)
    return;

  bitmap = malloc((size_t)bw * (size_t)bh);
  if (!bitmap)
    return;
  stbtt_MakeCodepointBitmap(&glyph_font, bitmap, bw, bh, bw, scale, scale, (int)cp);
  {
    Atlas *dst = &atlas;
    int cell_x = (int)(slot % atlas.cols);
    int cell_y = (int)(slot / atlas.cols);
    int atlas_width_px = dst->cell_width * dst->cols;
    int dst_cell_x_px = cell_x * dst->cell_width;
    int dst_cell_y_px = cell_y * dst->cell_height;
    int ascent_px = (int)(scale * (float)ascent);
    int row, col;

    for (row = 0; row < bh; row++) {
      for (col = 0; col < bw; col++) {
        int dst_x = dst_cell_x_px + col + x0;
        int dst_y = dst_cell_y_px + ascent_px + y0 + row;

        if (dst_x >= dst_cell_x_px && dst_x < dst_cell_x_px + (int)dst->cell_width &&
            dst_y >= dst_cell_y_px && dst_y < dst_cell_y_px + (int)dst->cell_height) {
          dst->atlas[dst_y * atlas_width_px + dst_x] =
            bitmap[row * bw + col];
        }
      }
    }
  }
  free(bitmap);
  glyph_atlas_dirty = true;
}

vt_glyph_id
vt_glyph_get(codepoint_t codepoint)
{
  u32 slot;
  int gi;

  if (codepoint < 128)
    return glyph_ascii[codepoint];

  slot = vt_lru_find(&glyph_lru, codepoint);
  if (slot != VT_LRU_NONE) {
    vt_lru_touch(&glyph_lru, slot);
    return vt_lru_pack(slot);
  }

  if (!(codepoint >= 0x2500 && codepoint <= 0x259F)) {
    gi = stbtt_FindGlyphIndex(&glyph_font, (int)codepoint);
    if (!gi && codepoint != UTF_INVALID) {
      vt_glyph_id packed;
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

  slot = vt_lru_alloc(&glyph_lru);
  if (slot == VT_LRU_NONE) {
    VTWARN("glyph atlas full");
    if (codepoint != UTF_INVALID)
      return vt_glyph_get(UTF_INVALID);
    return vt_lru_pack(0);
  }

  glyph_rasterize_slot(codepoint, slot);
  vt_lru_put(&glyph_lru, codepoint, slot,
      (codepoint >= VT_GLYPH_PIN_LO && codepoint <= VT_GLYPH_PIN_HI) || codepoint == UTF_INVALID);
  return vt_lru_pack(slot);
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
  new_atlas.rows = VT_ATLAS_ROWS;
  new_atlas.cols = VT_ATLAS_COLS;

  atlas_width_px  = (size_t) new_atlas.cell_width  * new_atlas.cols;
  atlas_height_px = (size_t) new_atlas.cell_height * new_atlas.rows;

  new_atlas.atlas = calloc(atlas_width_px * atlas_height_px, 1);
  if (!new_atlas.atlas) {
    free(glyph_ttf);
    glyph_ttf = NULL;
    return false;
  }

  atlas = new_atlas;
  glyph_atlas_dirty = false;
  vt_lru_init(&glyph_lru);

  for (i = 32; i < 128; i++) {
    u32 slot = vt_lru_alloc(&glyph_lru);
    glyph_rasterize_slot((codepoint_t)i, slot);
    vt_lru_put(&glyph_lru, (codepoint_t)i, slot, 1);
    glyph_ascii[i] = vt_lru_pack(slot);
  }
  if (stbtt_FindGlyphIndex(&glyph_font, (int)UTF_INVALID)) {
    u32 slot = vt_lru_alloc(&glyph_lru);
    glyph_rasterize_slot(UTF_INVALID, slot);
    vt_lru_put(&glyph_lru, UTF_INVALID, slot, 1);
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
}
