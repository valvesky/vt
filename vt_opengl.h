#ifndef _VT_OPENGL_H_
#define _VT_OPENGL_H_

#include "lib/glad.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "lib/stb_truetype.h"
#include "vt_vec.h"

#define FONT_MAX_LEN 128

enum {
  RENDER_PASS_BACKGROUND = 0,
  RENDER_PASS_GLYTHS,
};

typedef enum {
  RENDERER_UNIFORM_GRID = 0,
  RENDERER_UNIFORM_ATLAS,
  RENDERER_UNIFORM_PASS,
  COUNT_RENDERER_UNIFORM
} Renderer_Uniforms;

static_assert(COUNT_RENDERER_UNIFORM == 3, "Uniform count has changed, please update Renderer");

typedef struct {
  vec2i size;     
  vec2i bearing;
  float advance;
  vec2f uv_min;
  vec2f uv_max;
} Renderer_Character;

typedef struct {
  vec4f pos;
  vec4f uv;
  vec4f fg;
  vec4f bg;
} Renderer_Cell;

typedef enum {
  CELL_ATTR_POS = 0,
  CELL_ATTR_UV,
  CELL_ATTR_FG,
  CELL_ATTR_BG,
  ATTR_CELL_COUNT
} Renderer_Cell_Attr;

typedef struct {
  stbtt_fontinfo font_info;

  Renderer_Cell *cell_buffer;

  Renderer_Character glyth_table_ascii[128]; 

  GLuint vert_shader;
  GLuint frag_shader;
  GLuint program;
  GLuint vbo;
  GLuint vao;
  GLuint atlas_texture;

  /* maybe overkill but whatever */
  GLuint uniforms[COUNT_RENDERER_UNIFORM];

  uint32_t cell_size[2];
  float term_size[2];
  uint32_t cell_buffer_pos;
} Renderer;

Renderer Renderer_Create(const char * const font_path, size_t font_height, uint32_t screen_width, uint32_t screen_height);
void Renderer_Destroy(Renderer *renderer);
void Renderer_SetFont(Renderer *renderer, const char * const src);
void Renderer_ResizeScreen(Renderer *renderer, uint32_t width, uint32_t height);

#endif
