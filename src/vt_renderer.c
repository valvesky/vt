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
  RendPipeline compute;
  RendBuffer screen;
  RendBuffer alt;
  RendBuffer atlas_ssbo;
  RendBuffer glyph_ssbo;
  RendBuffer dest;
  RendTexture dest_tex;
#endif
  uint32_t current_width;
  uint32_t current_height;
};


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
static bool glyph_map_gpu_dirty;
static VtLRU glyph_lru;

#ifndef VT_HEADLESS
static bool renderer_init(void);
static bool renderer_cells_init(u32 cols, u32 rows);
static bool renderer_cells_resize(u32 cols, u32 rows);
static void renderer_cells_release_prev(void);
static TermCell *renderer_screen_cells(void);
static TermCell *renderer_alt_cells(void);
static u64 renderer_live_address(Term *t);
static void renderer_resize(u32 width, u32 height);
static void renderer_get_grid(Renderer *r, u32 *cols, u32 *rows);
static void renderer_sync(u64 cells, u32 cols, u32 rows, u32 cx, u32 cy, int cursor_on);
static void renderer_dest_release_prev(void);
static bool renderer_dest_ensure(u32 w, u32 h);
static void renderer_destroy(void);
static void glyph_map_upload(void);
static void glyph_atlas_upload(void);
#endif

static void *vt_file_alloc(const char *rel, unsigned long *n);
static bool glyph_table_init(const char *font_path, float pixel_height);
static void glyph_table_destroy(void);
static void renderer_unpack_rgb(color_packed_t packed, u8 *r, u8 *g, u8 *b);
static void renderer_apply_attr(u8 attr, u8 *fr, u8 *fg, u8 *fb, u8 *br, u8 *bg, u8 *bb);
static void renderer_cell_cursor(const TermCell *cell, color_packed_t cur_fg, color_packed_t cur_bg, codepoint_t *cp, color_packed_t *fg, color_packed_t *bg);
static bool renderer_screenshot_ppm(TermScreen *s, u32 cur_x, u32 cur_y, color_packed_t cur_fg, color_packed_t cur_bg, const char *path);
static void copy_bitmap_to_atlas(Atlas *atlas, int cell_x, int cell_y, const uint8_t *bitmap, int bw, int bh, int x0, int y0);
static void glyph_rasterize_slot(codepoint_t cp, u32 slot);
static int glyph_is_box(codepoint_t cp);
static void glyph_rasterize_box(codepoint_t cp, u32 slot);
static int glyph_pinned_cp(codepoint_t cp);

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
  RendPushConstantInfo pc;
  unsigned long comp_bytes = 0;
  uint8_t *comp_spv;
  size_t atlas_w;
  size_t atlas_h;
  size_t atlas_bytes;
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

  renderer.gpu = rend_renderer_create(&win, REND_BACKEND_AUTO, NULL, true, &bind_info);
  if (!renderer.gpu) {
    VTFATAL("rend_renderer_create");
    return false;
  }

  if (!glyph_table_init(font_path, (float) font_size_px)) {
    return false;
  }

  atlas_w = (size_t)atlas.cell_width * atlas.cols;
  atlas_h = (size_t)atlas.cell_height * atlas.rows;
  atlas_bytes = atlas_w * atlas_h * sizeof(u32);
  renderer.atlas_ssbo = rend_buffer_create(renderer.gpu, atlas_bytes, REND_BUFFER_STORAGE, false);
  renderer.glyph_ssbo = rend_buffer_create(renderer.gpu,
      VT_GLYPH_MAP_N * 2 * sizeof(u32), REND_BUFFER_STORAGE, false);
  if (!rend_buffer_mapped(&renderer.atlas_ssbo) || !rend_buffer_mapped(&renderer.glyph_ssbo)
      || !rend_buffer_address(&renderer.atlas_ssbo) || !rend_buffer_address(&renderer.glyph_ssbo)) {
    VTFATAL("atlas/glyph ssbo");
    return false;
  }
  glyph_map_gpu_dirty = true;
  glyph_atlas_dirty = true;

  pc.offset = 0;
  pc.size = sizeof(VtPush);
  comp_spv = vt_file_alloc("vulkan/vt.comp.spv", &comp_bytes);
  if (!comp_spv) {
    VTFATAL("failed to load SPIR-V");
    return false;
  }
  renderer.compute = rend_pipeline_create_compute_spirv(
      renderer.gpu, comp_spv, comp_bytes, &pc, 1);
  free(comp_spv);
  if (!renderer.compute) {
    VTFATAL("compute pipeline");
    return false;
  }

  glyph_atlas_upload();
  glyph_map_upload();
  bg = ansi_bg[bg_color];
  VTINFO("Peak+Rend atlas %ux%u cell %ux%u bg %06x",
      (unsigned)atlas_w, (unsigned)atlas_h,
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
static bool
renderer_cells_make(RendBuffer *screen, RendBuffer *alt, u32 cols, u32 rows)
{
  size_t bytes;

  if (!cols || !rows || !renderer.gpu)
    return false;
  bytes = (size_t)cols * (size_t)rows * sizeof(TermCell);
  *screen = rend_buffer_create(renderer.gpu, bytes, REND_BUFFER_STORAGE, false);
  *alt = rend_buffer_create(renderer.gpu, bytes, REND_BUFFER_STORAGE, false);
  return rend_buffer_mapped(screen) && rend_buffer_mapped(alt)
      && rend_buffer_address(screen) && rend_buffer_address(alt);
}

static bool
renderer_cells_init(u32 cols, u32 rows)
{
  if (!renderer_cells_make(&renderer.screen, &renderer.alt, cols, rows))
    return false;
  glyph_atlas_dirty = true;
  glyph_map_gpu_dirty = true;
  return true;
}

static RendBuffer cells_prev_screen;
static RendBuffer cells_prev_alt;
static RendBuffer dest_prev;
static RendTexture dest_prev_tex;
static u32 dest_w;
static u32 dest_h;

static bool
renderer_cells_resize(u32 cols, u32 rows)
{
  RendBuffer screen;
  RendBuffer alt;

  if (cells_prev_screen.handle)
    renderer_cells_release_prev();
  if (!renderer_cells_make(&screen, &alt, cols, rows))
    return false;
  cells_prev_screen = renderer.screen;
  cells_prev_alt = renderer.alt;
  renderer.screen = screen;
  renderer.alt = alt;
  glyph_atlas_dirty = true;
  glyph_map_gpu_dirty = true;
  return true;
}

static void
renderer_cells_release_prev(void)
{
  if (cells_prev_screen.handle)
    rend_buffer_destroy(&cells_prev_screen);
  if (cells_prev_alt.handle)
    rend_buffer_destroy(&cells_prev_alt);
  memset(&cells_prev_screen, 0, sizeof cells_prev_screen);
  memset(&cells_prev_alt, 0, sizeof cells_prev_alt);
}

static void
renderer_dest_release_prev(void)
{
  if (dest_prev.handle)
    rend_buffer_destroy(&dest_prev);
  if (dest_prev_tex.handle && renderer.gpu)
    rend_texture_destroy(renderer.gpu, &dest_prev_tex);
  memset(&dest_prev, 0, sizeof dest_prev);
  memset(&dest_prev_tex, 0, sizeof dest_prev_tex);
}

static bool
renderer_dest_ensure(u32 w, u32 h)
{
  size_t bytes;

  if (!w || !h || !renderer.gpu)
    return false;
  if (w == dest_w && h == dest_h && renderer.dest.handle && renderer.dest_tex.handle)
    return true;
  if (renderer.dest.handle || renderer.dest_tex.handle) {
    if (dest_prev.handle || dest_prev_tex.handle)
      renderer_dest_release_prev();
    dest_prev = renderer.dest;
    dest_prev_tex = renderer.dest_tex;
    memset(&renderer.dest, 0, sizeof renderer.dest);
    memset(&renderer.dest_tex, 0, sizeof renderer.dest_tex);
  }
  bytes = (size_t)w * (size_t)h * 4;
  renderer.dest = rend_buffer_create(renderer.gpu, bytes, REND_BUFFER_STORAGE, true);
  renderer.dest_tex = rend_texture_create(renderer.gpu, w, h, 1, 1, 1, REND_FORMAT_R8G8B8A8_UNORM);
  if (!rend_buffer_address(&renderer.dest) || !renderer.dest_tex.handle) {
    VTFATAL("dest buffer/tex");
    return false;
  }
  dest_w = w;
  dest_h = h;
  return true;
}

static TermCell *
renderer_screen_cells(void)
{
  return (TermCell *)rend_buffer_mapped(&renderer.screen);
}

static TermCell *
renderer_alt_cells(void)
{
  return (TermCell *)rend_buffer_mapped(&renderer.alt);
}

static u64
renderer_live_address(Term *t)
{
  if (t && (t->mode & TERM_MODE_ALTSCREEN))
    return rend_buffer_address(&renderer.alt);
  return rend_buffer_address(&renderer.screen);
}

static void
renderer_resize(u32 width, u32 height)
{
  renderer.current_width = width;
  renderer.current_height = height;
  VTDEBUG("Resize %ux%u", width, height);
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
renderer_sync(u64 cells, u32 cols, u32 rows, u32 cx, u32 cy, int cursor_on)
{
  VtPush pc;
  RendTexture *color;
  uint32_t bg;
  float r, g, b;
  u32 fb_w;
  u32 fb_h;
  u32 grid_w;
  u32 grid_h;

  if (!renderer.gpu || !renderer.compute || !cols || !rows)
    return;
  glyph_atlas_upload();
  glyph_map_upload();
  if (!rend_renderer_frame_begin(renderer.gpu))
    return;
  color = rend_renderer_color_target(renderer.gpu);
  if (!color) {
    rend_renderer_frame_end(renderer.gpu, NULL);
    return;
  }
  fb_w = rend_texture_width(color);
  fb_h = rend_texture_height(color);
  if (!fb_w)
    fb_w = renderer.current_width;
  if (!fb_h)
    fb_h = renderer.current_height;
  if (!renderer_dest_ensure(fb_w, fb_h)) {
    rend_renderer_frame_end(renderer.gpu, NULL);
    return;
  }

  memset(&pc, 0, sizeof pc);
  pc.cells = cells;
  pc.glyph_cp = rend_buffer_address(&renderer.glyph_ssbo);
  pc.glyph_slot = rend_buffer_address(&renderer.glyph_ssbo) + VT_GLYPH_MAP_N * sizeof(u32);
  pc.atlas = rend_buffer_address(&renderer.atlas_ssbo);
  pc.cols = cols;
  pc.rows = rows;
  pc.cell_w = atlas.cell_width;
  pc.cell_h = atlas.cell_height;
  pc.atlas_w = atlas.cell_width * atlas.cols;
  pc.atlas_h = atlas.cell_height * atlas.rows;
  pc.cursor_x = cx;
  pc.cursor_y = cy;
  pc.cursor_on = cursor_on ? 1u : 0u;
  pc.clear_bg = (u32)ansi_bg[bg_color] << 8;
  pc.alpha = alpha;
  pc.dest = rend_buffer_address(&renderer.dest);
  pc.fb_w = fb_w;
  pc.fb_h = fb_h;

  bg = ansi_bg[bg_color];
  r = (float)((bg >> 16) & 0xFF) / 255.0f;
  g = (float)((bg >> 8) & 0xFF) / 255.0f;
  b = (float)(bg & 0xFF) / 255.0f;
  rend_cmd_render_begin(renderer.gpu, r, g, b, alpha);
  rend_cmd_render_end(renderer.gpu);
  rend_cmd_bind_pipeline(renderer.compute);
  rend_cmd_push_constants(renderer.compute, &pc, sizeof pc);
  rend_cmd_dispatch(renderer.compute, cols, rows, 1);
  rend_cmd_copy_buffer_to_texture(renderer.gpu, &renderer.dest_tex, &renderer.dest);
  grid_w = cols * atlas.cell_width;
  grid_h = rows * atlas.cell_height;
  if (grid_w > fb_w)
    grid_w = fb_w;
  if (grid_h > fb_h)
    grid_h = fb_h;
  rend_cmd_blit(renderer.gpu, &renderer.dest_tex, color, 0, 0, grid_w, grid_h, 0, 0, grid_w, grid_h);
  rend_renderer_frame_end(renderer.gpu, NULL);
}

static void
renderer_destroy(void)
{
  glyph_table_destroy();
  renderer_cells_release_prev();
  renderer_dest_release_prev();
  if (renderer.dest.handle)
    rend_buffer_destroy(&renderer.dest);
  if (renderer.dest_tex.handle && renderer.gpu)
    rend_texture_destroy(renderer.gpu, &renderer.dest_tex);
  dest_w = dest_h = 0;
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
      fg = cell->fg;
      bg = cell->bg;
      if (at_cursor)
        renderer_cell_cursor(cell, cur_fg, cur_bg, &cp, &fg, &bg);

      if (!cp && !cell->fg && !cell->bg && !at_cursor)
        continue;
      if (cp == 0)
        cp = (codepoint_t)' ';

      idx = vt_glyph_get(cp);
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

static int
glyph_pinned_cp(codepoint_t cp)
{
  return (cp >= VT_GLYPH_PIN_LO && cp <= VT_GLYPH_PIN_HI) || cp == UTF_INVALID;
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
  size_t n;
  size_t i;
  u32 *dst;

  if (!glyph_atlas_dirty || !renderer.gpu || !atlas.atlas
      || !rend_buffer_mapped(&renderer.atlas_ssbo))
    return;
  n = (size_t)atlas.cell_width * atlas.cols * (size_t)atlas.cell_height * atlas.rows;
  dst = rend_buffer_mapped(&renderer.atlas_ssbo);
  for (i = 0; i < n; i++)
    dst[i] = atlas.atlas[i];
  glyph_atlas_dirty = false;
}

static void
glyph_map_upload(void)
{
  u32 *dst;

  if (!glyph_map_gpu_dirty || !renderer.gpu || !rend_buffer_mapped(&renderer.glyph_ssbo))
    return;
  dst = rend_buffer_mapped(&renderer.glyph_ssbo);
  memcpy(dst, glyph_lru.cp, sizeof glyph_lru.cp);
  memcpy(dst + VT_GLYPH_MAP_N, glyph_lru.slot, sizeof glyph_lru.slot);
  glyph_map_gpu_dirty = false;
}
#endif

vt_glyph_id
vt_glyph_get(codepoint_t codepoint)
{
  u32 slot;
  int gi;

  if (codepoint == 0)
    codepoint = (codepoint_t)' ';

  slot = vt_lru_find(&glyph_lru, codepoint);
  if (slot != VT_LRU_NONE) {
    vt_lru_touch(&glyph_lru, slot);
    return vt_lru_pack(slot);
  }

  if (!glyph_is_box(codepoint)) {
    gi = stbtt_FindGlyphIndex(&glyph_font, (int)codepoint);
    if (!gi && codepoint != UTF_INVALID) {
      vt_glyph_id packed;
      u32 fffd;

      packed = vt_glyph_get(UTF_INVALID);
      fffd = vt_lru_find(&glyph_lru, UTF_INVALID);
      if (fffd != VT_LRU_NONE)
        vt_lru_alias(&glyph_lru, codepoint, fffd);
      glyph_map_gpu_dirty = true;
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
  vt_lru_put(&glyph_lru, codepoint, slot, glyph_pinned_cp(codepoint));
  glyph_map_gpu_dirty = true;
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
  glyph_map_gpu_dirty = true;
  vt_lru_init(&glyph_lru);

  for (i = 32; i < 128; i++) {
    u32 slot = vt_lru_alloc(&glyph_lru);
    glyph_rasterize_slot((codepoint_t)i, slot);
    vt_lru_put(&glyph_lru, (codepoint_t)i, slot, 1);
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
