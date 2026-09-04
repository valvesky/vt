#ifndef VT_RENDERER_H
#define VT_RENDERER_H

typedef struct Renderer Renderer;

enum {
    VT_ATLAS_COLS   = 30,
    VT_ATLAS_ROWS   = 30,
    VT_GLYPH_N      = VT_ATLAS_COLS * VT_ATLAS_ROWS,
    VT_GLYPH_MAP_N  = 2048,
};

STATIC_ASSERT(VT_GLYPH_N == VT_ATLAS_COLS * VT_ATLAS_ROWS, "atlas slots");

typedef struct VtPush {
  f32 ndc_x;
  f32 ndc_y;
  f32 uv_x;
  f32 uv_y;
  f32 alpha;
} VtPush;

typedef struct VtPushTile {
  u32 cell_w;
  u32 cell_h;
  u32 cols;
  u32 rows;
  f32 uv_x;
  f32 uv_y;
  f32 alpha;
  u32 def_bg;
} VtPushTile;

typedef struct VtInstance {
  u32 pos;
  u32 foreground;
  u32 background;
} VtInstance;

STATIC_ASSERT(sizeof (VtInstance) == 12, "VtInstance is 12 bytes");
#define VT_GLYPH_COLOR 0x01000000u

STATIC_ASSERT(VT_ATLAS_COLS <= 31, "glyph col lives in 5 bits");
STATIC_ASSERT(VT_ATLAS_ROWS <= 127, "glyph row lives in 7 bits; bit 7 is wide");

typedef u32 vt_glyph_id;

vt_glyph_id vt_glyph_get(codepoint_t cp);
vt_glyph_id vt_glyph_get_run(const codepoint_t *cps, u32 n);

#endif
