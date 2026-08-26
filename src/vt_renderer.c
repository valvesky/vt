#pragma once
#include "config.h"
#include "vt.h"

#define ATLAS_ROWS 30
#define ATLAS_COLS 30
#define GLYTH_BUFFER_MAX 32768
#define GLYPH_MAP_N 2048
#define GLYPH_MAP_NONE 0xffffffffu

struct Renderer_Cell {
  u32 pos;
  u32 glyth_index;
  u32 foreground;
  u32 background;
};

#ifndef VT_HEADLESS
static const RendVertexAttributes renderer_cell_attrs[] = {
  { .location = 0, .binding = 0, .offset = offsetof(Renderer_Cell, pos),        .format = REND_FORMAT_R32_UINT },
  { .location = 1, .binding = 0, .offset = offsetof(Renderer_Cell, glyth_index), .format = REND_FORMAT_R32_UINT },
  { .location = 2, .binding = 0, .offset = offsetof(Renderer_Cell, foreground),  .format = REND_FORMAT_R32_UINT },
  { .location = 3, .binding = 0, .offset = offsetof(Renderer_Cell, background),  .format = REND_FORMAT_R32_UINT },
};
#endif

typedef struct Atlas {
  unsigned char *atlas;
  uint32_t cell_width;
  uint32_t cell_height;
  uint32_t rows;
  uint32_t cols;
} Atlas;

typedef struct {
  f32 grid_x;
  f32 grid_y;
  uint32_t atlas_cell_width;
  uint32_t atlas_cell_height;
  uint32_t atlas_width;
  uint32_t atlas_height;
  f32 alpha;
  uint32_t _pad2;
} Renderer_Info;

struct Renderer {
#ifndef VT_HEADLESS
  RendRenderer gpu;
  RendPipeline pipeline;
  RendBuffer instance_buf;
  RendBuffer ubo;
  RendTexture atlas_tex;
#endif
  Renderer_Info info_ubo;
  uint32_t current_width;
  uint32_t current_height;
};

typedef uint32_t atlas_index_packed;

#ifndef VT_HEADLESS
static PeakWindow win;
static Renderer renderer;
static Renderer_Cell glyth_buffer[GLYTH_BUFFER_MAX];
static u32 glyth_buffer_pos = 0;
#endif
static Atlas atlas;
static int ascent = 0;
static int descent = 0;
static int line_gap = 0;
static float scale = 0.0;
static stbtt_fontinfo glyph_font;
static unsigned char *glyph_ttf;
static u32 glyph_next_slot;
static u32 glyph_slot_cap;
static bool glyph_atlas_dirty;
static codepoint_t glyph_map_cp[GLYPH_MAP_N];
static atlas_index_packed glyph_map_idx[GLYPH_MAP_N];

#ifndef VT_HEADLESS
static bool renderer_init(void);
#endif
static void renderer_cell_cursor(const TermCell *cell, color_packed_t cur_fg, color_packed_t cur_bg, codepoint_t *cp, color_packed_t *fg, color_packed_t *bg);
#ifndef VT_HEADLESS
static void renderer_draw_codepoint(codepoint_t c, u32 x, u32 y, color_packed_t fg, color_packed_t bg);
static void renderer_draw_screen(TermScreen *s);
static void renderer_apply_grid(void);
static void renderer_resize(u32 width, u32 height);
static void renderer_get_grid(Renderer *r, u32 *cols, u32 *rows);
static void renderer_sync(void);
static void renderer_destroy(void);
#endif

static void *vt_file_alloc(const char *rel, unsigned long *n);
static bool glyth_table_init(const char *font_path, float pixel_height);
static void glyth_table_destroy(void);
static void renderer_unpack_rgb(color_packed_t packed, u8 *r, u8 *g, u8 *b);
static void renderer_apply_attr(u8 attr, u8 *fr, u8 *fg, u8 *fb, u8 *br, u8 *bg, u8 *bb);
static bool renderer_screenshot_ppm(TermScreen *s, u32 cur_x, u32 cur_y, color_packed_t cur_fg, color_packed_t cur_bg, const char *path);
static atlas_index_packed glyth_table_get(codepoint_t);
static void copy_bitmap_to_atlas(Atlas *atlas, int cell_x, int cell_y, const uint8_t *bitmap, int bw, int bh, int x0, int y0);
#ifdef DEBUG
static void dump_atlas_to_pgm(Atlas *atlas, const char *path);
#endif
static atlas_index_packed glyph_pack_slot(u32 slot);
static u32 glyph_map_lookup(codepoint_t cp, int for_insert);
static void glyph_map_put(codepoint_t cp, atlas_index_packed idx);
static void glyph_rasterize_slot(codepoint_t cp, u32 slot);
#ifndef VT_HEADLESS
static void glyph_atlas_upload(void);
#endif
static int glyph_is_box(codepoint_t cp);
static void glyph_rasterize_box(codepoint_t cp, u32 slot);

#ifdef VT_HEADLESS
static void *
vt_file_read(const char *path, unsigned long *n)
{
  FILE *f;
  void *p;
  long sz;

  f = fopen(path, "rb");
  if (!f)
    return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return NULL;
  }
  rewind(f);
  p = malloc((size_t)sz + 1);
  if (!p) {
    fclose(f);
    return NULL;
  }
  if (sz > 0 && fread(p, 1, (size_t)sz, f) != (size_t)sz) {
    free(p);
    fclose(f);
    return NULL;
  }
  fclose(f);
  ((unsigned char *)p)[sz] = 0;
  if (n)
    *n = (unsigned long)sz;
  return p;
}
#endif

static void *
vt_file_alloc(const char *rel, unsigned long *n)
{
  void *p;
  char buf[256];

#ifndef VT_HEADLESS
  p = peak_file_alloc(rel, n);
#else
  p = vt_file_read(rel, n);
#endif
  if (p)
    return p;
  snprintf(buf, sizeof buf, "/usr/share/vt/%s", rel);
#ifndef VT_HEADLESS
  return peak_file_alloc(buf, n);
#else
  return vt_file_read(buf, n);
#endif
}

#ifndef VT_HEADLESS
static bool
renderer_init(void)
{
  RendBindingInfo bind_info = {0};
  unsigned long vert_bytes = 0;
  unsigned long frag_bytes = 0;
  uint8_t *vert_spv;
  uint8_t *frag_spv;
  RendVertexBinding vbind;
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

  bind_info.ubo_bindings[0] = 0;
  bind_info.ubo_array_sizes[0] = 1;
  bind_info.ubo_binding_count = 1;
  bind_info.texture_bindings[0] = 1;
  bind_info.texture_array_sizes[0] = 1;
  bind_info.texture_binding_count = 1;

  renderer.gpu = rend_renderer_create(&win, REND_BACKEND_AUTO, NULL, true, &bind_info);
  if (!renderer.gpu) {
    VTFATAL("rend_renderer_create");
    return false;
  }

  if (!glyth_table_init(font_path, (float) font_size_px)) {
    return false;
  }

#ifdef DEBUG
  dump_atlas_to_pgm(&atlas, "atlas.pgm");
#endif

  atlas_w = (size_t) atlas.cell_width * atlas.cols;
  atlas_h = (size_t) atlas.cell_height * atlas.rows;
  renderer.atlas_tex = rend_texture_create_from_data(
      renderer.gpu, atlas.atlas, (uint32_t) atlas_w, (uint32_t) atlas_h, REND_FORMAT_R8_UNORM);

  renderer.instance_buf = rend_buffer_create(
      renderer.gpu, GLYTH_BUFFER_MAX * sizeof *glyth_buffer, REND_BUFFER_VERTEX, false);
  renderer.ubo = rend_buffer_create(
      renderer.gpu, sizeof(Renderer_Info), REND_BUFFER_UNIFORM, false);

  renderer.info_ubo = (Renderer_Info) {
    .atlas_width = (uint32_t) atlas_w,
    .atlas_height = (uint32_t) atlas_h,
    .atlas_cell_width = atlas.cell_width,
    .atlas_cell_height = atlas.cell_height,
    .alpha = alpha,
  };
  renderer_apply_grid();
  rend_buffer_write(renderer.gpu, &renderer.ubo, &renderer.info_ubo, sizeof renderer.info_ubo, 0);

  rend_descriptor_write_ubo(renderer.gpu, renderer.ubo, 0, 0);
  rend_descriptor_write_texture(renderer.gpu, &renderer.atlas_tex, 1, 0);

  vert_spv = vt_file_alloc("vulkan/vt.vert.spv", &vert_bytes);
  frag_spv = vt_file_alloc("vulkan/vt.frag.spv", &frag_bytes);
  if (!vert_spv || !frag_spv) {
    VTFATAL("failed to load glyph SPIR-V");
    free(vert_spv);
    free(frag_spv);
    return false;
  }

  vbind = (RendVertexBinding) {
    .binding = 0,
    .stride = sizeof(Renderer_Cell),
    .input_rate = REND_INPUT_RATE_INSTANCE,
  };

  renderer.pipeline = rend_pipeline_create_graphics_spirv(
      renderer.gpu,
      vert_spv, vert_bytes,
      frag_spv, frag_bytes,
      &vbind, 1,
      renderer_cell_attrs, 4,
      NULL, 0,
      REND_POLYGON_MODE_FILL,
      REND_CULL_MODE_NONE,
      REND_TOPOLOGY_TRIANGLE_STRIP,
      REND_FORMAT_UNDEFINED,
      false);
  free(vert_spv);
  free(frag_spv);
  if (!renderer.pipeline) {
    VTFATAL("rend_pipeline_create_graphics_spirv");
    return false;
  }

  bg = ansi_bg[bg_color];
  VTINFO("Peak+Rend atlas %ux%u cell %ux%u bg %06x",
      (unsigned) atlas_w, (unsigned) atlas_h,
      atlas.cell_width, atlas.cell_height, bg);
  return true;
}
#endif

static void
renderer_cell_cursor(const TermCell *cell, color_packed_t cur_fg, color_packed_t cur_bg,
    codepoint_t *cp, color_packed_t *fg, color_packed_t *bg)
{
  if (cell->codepoint) {
    *cp = cell->codepoint;
    *fg = cell->bg;
    *bg = cell->fg;
  } else {
    *cp = (codepoint_t)' ';
    *fg = cur_bg;
    *bg = cur_fg;
  }
}

#ifndef VT_HEADLESS
static void
renderer_draw_codepoint(codepoint_t c, u32 x, u32 y, color_packed_t fg, color_packed_t bg)
{
  if (glyth_buffer_pos < GLYTH_BUFFER_MAX) {
    glyth_buffer[glyth_buffer_pos++] = (Renderer_Cell) {
        .pos = x << 16 | y,
        .glyth_index = glyth_table_get(c),
        .foreground = fg,
        .background = bg,
    };
  } else {
    VTWARN("Reached maximum number of glyths!");
  }
}

static void
renderer_draw_screen(TermScreen *s)
{
  TermCell *start = s->cell_buffer;
  TermCell *ptr = start;
  const TermCell *end = &s->cell_buffer[s->cols * s->rows];

  for (; ptr < end; ptr++) {
    u32 idx;
    codepoint_t cp;

    if (!ptr->codepoint && !ptr->fg && !ptr->bg)
      continue;
    idx = (u32)(ptr - start);
    cp = ptr->codepoint ? ptr->codepoint : (codepoint_t)' ';
    renderer_draw_codepoint(cp, idx % s->cols, idx / s->cols, ptr->fg, ptr->bg);
  }
}

static void
renderer_apply_grid(void)
{
  u32 cols, rows;

  cols = atlas.cell_width ? renderer.current_width / atlas.cell_width : 0;
  rows = atlas.cell_height ? renderer.current_height / atlas.cell_height : 0;
  renderer.info_ubo.grid_x = (float)(cols ? cols : 1);
  renderer.info_ubo.grid_y = (float)(rows ? rows : 1);
}

static void
renderer_resize(u32 width, u32 height)
{
  renderer.current_width = width;
  renderer.current_height = height;
  renderer_apply_grid();
  VTDEBUG("Resize grid = %fx%f", renderer.info_ubo.grid_x, renderer.info_ubo.grid_y);
  rend_buffer_write(renderer.gpu, &renderer.ubo, &renderer.info_ubo, sizeof renderer.info_ubo, 0);
}

static void
renderer_get_grid(Renderer *r, u32 *cols, u32 *rows)
{
  *cols = (u32)r->info_ubo.grid_x;
  *rows = (u32)r->info_ubo.grid_y;
}

extern void
renderer_sync(void)
{
  uint32_t bg = ansi_bg[bg_color];
  float r = (float) ((bg >> 16) & 0xFF) / 255.0f;
  float g = (float) ((bg >> 8) & 0xFF) / 255.0f;
  float b = (float) (bg & 0xFF) / 255.0f;

  if (!renderer.gpu) return;
  glyph_atlas_upload();
  if (!rend_renderer_frame_begin(renderer.gpu)) {
    glyth_buffer_pos = 0;
    return;
  }

  rend_cmd_render_begin(renderer.gpu, r, g, b, alpha); {
    if (glyth_buffer_pos > 0) {
      rend_buffer_write(renderer.gpu, &renderer.instance_buf,
          glyth_buffer, glyth_buffer_pos * sizeof *glyth_buffer, 0);
      rend_cmd_bind_pipeline(renderer.pipeline);
      rend_cmd_bind_vertex_buffer(renderer.pipeline, 0, renderer.instance_buf, 0);
      rend_cmd_draw(renderer.pipeline, 4, glyth_buffer_pos);
    }
  } rend_cmd_render_end(renderer.gpu);

  rend_renderer_frame_end(renderer.gpu, NULL);
  glyth_buffer_pos = 0;
}

extern void
renderer_destroy(void)
{
  glyth_table_destroy();
  if (renderer.gpu) {
    rend_quit();
    renderer.gpu = NULL;
  }
  if (win.running) {
    peak_window_close(&win);
  }
  peak_quit();
  memset(&renderer, 0, sizeof renderer);
}
#endif

static void
copy_bitmap_to_atlas(
    Atlas *dst,
    int cell_x, int cell_y,
    const uint8_t *bitmap,
    int bw, int bh,
    int x0, int y0)
{
  int atlas_width_px  = dst->cell_width  * dst->cols;
  int dst_cell_x_px = cell_x * dst->cell_width;
  int dst_cell_y_px = cell_y * dst->cell_height;
  int ascent_px = (int) (scale * (float) ascent);
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

static void
renderer_unpack_rgb(color_packed_t packed, u8 *r, u8 *g, u8 *b)
{
  *r = (u8)((packed >> 24) & 0xff);
  *g = (u8)((packed >> 16) & 0xff);
  *b = (u8)((packed >> 8) & 0xff);
}

static void
renderer_apply_attr(u8 attr, u8 *fr, u8 *fg, u8 *fb, u8 *br, u8 *bg, u8 *bb)
{
  if (attr & TERM_ATTR_REVERSE) {
    u8 tr = *fr, tg = *fg, tb = *fb;
    *fr = *br; *fg = *bg; *fb = *bb;
    *br = tr; *bg = tg; *bb = tb;
  }
  if (attr & TERM_ATTR_INVISIBLE) {
    *fr = *br; *fg = *bg; *fb = *bb;
  }
  if (attr & TERM_ATTR_BOLD) {
    *fr = (u8)MIN(255, (int)*fr + 30);
    *fg = (u8)MIN(255, (int)*fg + 30);
    *fb = (u8)MIN(255, (int)*fb + 30);
  }
  if (attr & TERM_ATTR_FAINT) {
    *fr = (u8)((int)*fr * 3 / 5);
    *fg = (u8)((int)*fg * 3 / 5);
    *fb = (u8)((int)*fb * 3 / 5);
  }
}

static bool
renderer_screenshot_ppm(TermScreen *s, u32 cur_x, u32 cur_y, color_packed_t cur_fg, color_packed_t cur_bg, const char *path)
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
      atlas_index_packed idx;
      u32 ax;
      u32 ay;
      u32 py;
      u32 px;
      u8 fr, fg8, fb, br, bg8, bb;
      bool at_cursor;

      cell = &s->cell_buffer[y * s->cols + x];
      at_cursor = (x == cur_x && y == cur_y);
      cp = cell->codepoint;
      fg = cell->fg;
      bg = cell->bg;
      if (at_cursor)
        renderer_cell_cursor(cell, cur_fg, cur_bg, &cp, &fg, &bg);

      if (!cp && !cell->fg && !cell->bg && !at_cursor)
        continue;
      if (cp == 0)
        cp = (codepoint_t)' ';

      idx = glyth_table_get(cp);
      ax = idx >> 16;
      ay = idx & 0xffff;
      renderer_unpack_rgb(fg, &fr, &fg8, &fb);
      renderer_unpack_rgb(bg, &br, &bg8, &bb);
      renderer_apply_attr((u8)(fg & 0xff), &fr, &fg8, &fb, &br, &bg8, &bb);
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

#ifdef DEBUG
static void
dump_atlas_to_pgm(Atlas *src, const char *path)
{
  unsigned char *data = (unsigned char*) src->atlas;
  uint32_t width = src->cell_width * src->cols;
  uint32_t height = src->cell_height * src->rows;
  FILE *f = fopen(path, "wb");
  if (!f) {
    VTERROR("fopen");
    return;
  }
  fprintf(f, "P5\n%d %d\n255\n", width, height);
  fwrite(data, 1, width * height, f);
  fclose(f);
}
#endif

static atlas_index_packed
glyph_pack_slot(u32 slot)
{
  return ((slot % atlas.cols) << 16) | (slot / atlas.cols);
}

static u32
glyph_map_lookup(codepoint_t cp, int for_insert)
{
  u32 h;
  u32 i;

  h = (cp * 2654435761u) & (GLYPH_MAP_N - 1);
  for (i = 0; i < GLYPH_MAP_N; i++) {
    u32 s = (h + i) & (GLYPH_MAP_N - 1);
    if (glyph_map_cp[s] == cp)
      return s;
    if (glyph_map_cp[s] == 0)
      return for_insert ? s : GLYPH_MAP_NONE;
  }
  return GLYPH_MAP_NONE;
}

static void
glyph_map_put(codepoint_t cp, atlas_index_packed idx)
{
  u32 s = glyph_map_lookup(cp, 1);
  if (s == GLYPH_MAP_NONE)
    return;
  glyph_map_cp[s] = cp;
  glyph_map_idx[s] = idx;
}

static int
glyph_is_box(codepoint_t cp)
{
  return (cp >= 0x2500 && cp <= 0x259F);
}

static void
glyph_box_hline(uint8_t *cell, int w, int h, int y, int x0, int x1, int thick)
{
  int t, x, yy;
  int y0 = y - thick / 2;

  for (t = 0; t < thick; t++) {
    yy = y0 + t;
    if (yy < 0 || yy >= h)
      continue;
    for (x = x0; x < x1; x++) {
      if (x >= 0 && x < w)
        cell[yy * w + x] = 255;
    }
  }
}

static void
glyph_box_vline(uint8_t *cell, int w, int h, int x, int y0, int y1, int thick)
{
  int t, y, xx;
  int x0 = x - thick / 2;

  for (t = 0; t < thick; t++) {
    xx = x0 + t;
    if (xx < 0 || xx >= w)
      continue;
    for (y = y0; y < y1; y++) {
      if (y >= 0 && y < h)
        cell[y * w + xx] = 255;
    }
  }
}

static void
glyph_box_weights(codepoint_t cp, int *n, int *e, int *s, int *w)
{
  *n = *e = *s = *w = 0;
  switch (cp) {
  case 0x2500: *e = *w = 1; break;
  case 0x2501: *e = *w = 2; break;
  case 0x2502: *n = *s = 1; break;
  case 0x2503: *n = *s = 2; break;
  case 0x250C: *e = *s = 1; break;
  case 0x250F: *e = *s = 2; break;
  case 0x2510: *w = *s = 1; break;
  case 0x2513: *w = *s = 2; break;
  case 0x2514: *e = *n = 1; break;
  case 0x2517: *e = *n = 2; break;
  case 0x2518: *w = *n = 1; break;
  case 0x251B: *w = *n = 2; break;
  case 0x251C: *n = *s = *e = 1; break;
  case 0x2520: *n = *s = 2; *e = 1; break;
  case 0x2523: *n = *s = *e = 2; break;
  case 0x2524: *n = *s = *w = 1; break;
  case 0x2528: *n = *s = 2; *w = 1; break;
  case 0x252B: *n = *s = *w = 2; break;
  case 0x252C: *e = *w = *s = 1; break;
  case 0x252F: *e = *w = 2; *s = 1; break;
  case 0x2533: *e = *w = *s = 2; break;
  case 0x2534: *e = *w = *n = 1; break;
  case 0x2537: *e = *w = 2; *n = 1; break;
  case 0x253B: *e = *w = *n = 2; break;
  case 0x253C: *n = *e = *s = *w = 1; break;
  case 0x254B: *n = *e = *s = *w = 2; break;
  case 0x2574: *w = 1; break;
  case 0x2575: *n = 1; break;
  case 0x2576: *e = 1; break;
  case 0x2577: *s = 1; break;
  case 0x2578: *w = 2; break;
  case 0x2579: *n = 2; break;
  case 0x257A: *e = 2; break;
  case 0x257B: *s = 2; break;
  case 0x2550: *e = *w = 2; break;
  case 0x2551: *n = *s = 2; break;
  case 0x2554: *e = *s = 2; break;
  case 0x2557: *w = *s = 2; break;
  case 0x255A: *e = *n = 2; break;
  case 0x255D: *w = *n = 2; break;
  case 0x2560: *n = *s = *e = 2; break;
  case 0x2563: *n = *s = *w = 2; break;
  case 0x2566: *e = *w = *s = 2; break;
  case 0x2569: *e = *w = *n = 2; break;
  case 0x256C: *n = *e = *s = *w = 2; break;
  case 0x256D: *e = *s = 1; break;
  case 0x256E: *w = *s = 1; break;
  case 0x256F: *w = *n = 1; break;
  case 0x2570: *e = *n = 1; break;
  default:
    break;
  }
}

static void
glyph_rasterize_box(codepoint_t cp, u32 slot)
{
  int cw = (int)atlas.cell_width;
  int ch = (int)atlas.cell_height;
  int atlas_w = cw * (int)atlas.cols;
  int cell_x = (int)(slot % atlas.cols) * cw;
  int cell_y = (int)(slot / atlas.cols) * ch;
  int mx = cw / 2;
  int my = ch / 2;
  int light = cw >= 16 ? 2 : 1;
  int heavy = cw >= 16 ? 4 : 2;
  int n, e, s, w;
  uint8_t *tmp;
  int row;

  tmp = calloc((size_t)cw * (size_t)ch, 1);
  if (!tmp)
    return;

  if (cp >= 0x2580 && cp <= 0x259F) {
    int y0 = 0, y1 = ch, x0 = 0, x1 = cw;
    int x, y;
    if (cp == 0x2580) { y0 = 0; y1 = ch / 2; }
    else if (cp == 0x2584) { y0 = ch / 2; y1 = ch; }
    else if (cp == 0x2588) { y0 = 0; y1 = ch; }
    else if (cp == 0x258C) { x1 = cw / 2; }
    else if (cp == 0x2590) { x0 = cw / 2; }
    else if (cp == 0x2591 || cp == 0x2592 || cp == 0x2593) {
      uint8_t cover = (cp == 0x2591) ? 64 : (cp == 0x2592) ? 128 : 192;
      for (y = 0; y < ch; y++)
        for (x = 0; x < cw; x++)
          tmp[y * cw + x] = cover;
      goto blit;
    }
    for (y = y0; y < y1; y++)
      for (x = x0; x < x1; x++)
        tmp[y * cw + x] = 255;
    goto blit;
  }

  glyph_box_weights(cp, &n, &e, &s, &w);
  if (w)
    glyph_box_hline(tmp, cw, ch, my, 0, mx + (w == 2 ? heavy : light) / 2, w == 2 ? heavy : light);
  if (e)
    glyph_box_hline(tmp, cw, ch, my, mx - (e == 2 ? heavy : light) / 2, cw, e == 2 ? heavy : light);
  if (n)
    glyph_box_vline(tmp, cw, ch, mx, 0, my + (n == 2 ? heavy : light) / 2, n == 2 ? heavy : light);
  if (s)
    glyph_box_vline(tmp, cw, ch, mx, my - (s == 2 ? heavy : light) / 2, ch, s == 2 ? heavy : light);

blit:
  for (row = 0; row < ch; row++) {
    memcpy(atlas.atlas + (size_t)(cell_y + row) * (size_t)atlas_w + (size_t)cell_x,
        tmp + (size_t)row * (size_t)cw, (size_t)cw);
  }
  free(tmp);
  glyph_atlas_dirty = true;
}

static void
glyph_rasterize_slot(codepoint_t cp, u32 slot)
{
  int x0, y0, x1, y1;
  int bw, bh;
  uint8_t *bitmap;

  if (glyph_is_box(cp)) {
    int n = 0, e = 0, s = 0, w = 0;
    if (cp >= 0x2580 && cp <= 0x259F) {
      glyph_rasterize_box(cp, slot);
      return;
    }
    glyph_box_weights(cp, &n, &e, &s, &w);
    if (n | e | s | w) {
      glyph_rasterize_box(cp, slot);
      return;
    }
  }

  stbtt_GetCodepointBitmapBox(&glyph_font, (int)cp, scale, scale, &x0, &y0, &x1, &y1);
  bw = x1 - x0;
  bh = y1 - y0;
  if (bw <= 0 || bh <= 0)
    return;

  bitmap = malloc((size_t)bw * (size_t)bh);
  if (!bitmap)
    return;
  stbtt_MakeCodepointBitmap(&glyph_font, bitmap, bw, bh, bw, scale, scale, (int)cp);
  copy_bitmap_to_atlas(&atlas, (int)(slot % atlas.cols), (int)(slot / atlas.cols),
      bitmap, bw, bh, x0, y0);
  free(bitmap);
  glyph_atlas_dirty = true;
}

#ifndef VT_HEADLESS
static void
glyph_atlas_upload(void)
{
  size_t bytes;

  if (!glyph_atlas_dirty || !renderer.gpu || !atlas.atlas)
    return;
  bytes = (size_t)atlas.cell_width * atlas.cols * (size_t)atlas.cell_height * atlas.rows;
  rend_texture_copy_data(renderer.gpu, &renderer.atlas_tex, atlas.atlas, bytes);
  glyph_atlas_dirty = false;
}
#endif

static atlas_index_packed
glyth_table_get(codepoint_t codepoint)
{
  u32 slot;
  u32 s;
  atlas_index_packed idx;
  int gi;

  if (codepoint == 0)
    return 0;

  s = glyph_map_lookup(codepoint, 0);
  if (s != GLYPH_MAP_NONE)
    return glyph_map_idx[s];

  gi = stbtt_FindGlyphIndex(&glyph_font, (int)codepoint);
  if (!gi && codepoint != UTF_INVALID) {
    return glyth_table_get(UTF_INVALID);
  }
  if (!gi && codepoint == UTF_INVALID) {
    return glyth_table_get((codepoint_t)'?');
  }

  if (glyph_next_slot >= glyph_slot_cap) {
    VTWARN("glyph atlas full");
    if (codepoint != UTF_INVALID)
      return glyth_table_get(UTF_INVALID);
    return 0;
  }

  slot = glyph_next_slot++;
  glyph_rasterize_slot(codepoint, slot);
  idx = glyph_pack_slot(slot);
  glyph_map_put(codepoint, idx);
  return idx;
}

static bool
glyth_table_init(const char *path, float pixel_height)
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
  new_atlas.rows = ATLAS_ROWS;
  new_atlas.cols = ATLAS_COLS;

  atlas_width_px  = (size_t) new_atlas.cell_width  * new_atlas.cols;
  atlas_height_px = (size_t) new_atlas.cell_height * new_atlas.rows;

  new_atlas.atlas = calloc(atlas_width_px * atlas_height_px, 1);
  if (!new_atlas.atlas) {
    free(glyph_ttf);
    glyph_ttf = NULL;
    return false;
  }

  atlas = new_atlas;
  glyph_slot_cap = atlas.rows * atlas.cols;
  glyph_next_slot = 0;
  glyph_atlas_dirty = false;
  memset(glyph_map_cp, 0, sizeof glyph_map_cp);
  memset(glyph_map_idx, 0, sizeof glyph_map_idx);

  for (i = 32; i < 128; i++) {
    u32 slot = glyph_next_slot++;
    glyph_rasterize_slot((codepoint_t)i, slot);
    glyph_map_put((codepoint_t)i, glyph_pack_slot(slot));
  }
  if (stbtt_FindGlyphIndex(&glyph_font, (int)UTF_INVALID)) {
    u32 slot = glyph_next_slot++;
    glyph_rasterize_slot(UTF_INVALID, slot);
    glyph_map_put(UTF_INVALID, glyph_pack_slot(slot));
  }

  /* CPU atlas is uploaded once from renderer_init; not dirty until a miss. */
  glyph_atlas_dirty = false;
  return true;
}

static void
glyth_table_destroy(void)
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
