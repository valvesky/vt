#pragma once
#include "vt.h"
#include "vt_debug.h"

/* For the OpenGL3 renderer a lot of the modern features are either not available 
 * or not exposed by the drivers (thanks a lot intel)
 *
 * For a terminal emulator that means we cant just slap a compute shader 
 * on an SSBO containing all of the cells and call it a day.
 *
 * We only really care about displaying visible cells anyways so instacing
 * is more than enougth. Honestly this way of implementing might be better than
 * throwing everything into the GPU, but I'm not a graphics programmer so I
 * can't really say.
 */

/* 900 glyths in the texture at any time
 * should be more than enough for most cases */
#define ATLAS_ROWS 30
#define ATLAS_COLS 30
#define GLYTH_TABLE_MAX (ATLAS_COLS * ATLAS_ROWS)
#define GLYTH_BUFFER_MAX 10000

/* on windows wchar_t is not 32 bits
 * so to avoid confusion codepoint_t is defined */
#pragma GCC poison wchar_t 
typedef uint32_t codepoint_t;

typedef struct Atlas {
  unsigned char *atlas;
  GLuint texture;
  uint32_t cell_width;
  uint32_t cell_height;
  uint32_t rows;
  uint32_t cols;
} Atlas;

typedef float f32;
static_assert(sizeof(f32)==4, "f32 is not 32 bits!");

typedef struct {
  f32 grid_x;
  f32 grid_y; 
  uint32_t atlas_cell_width;
  uint32_t atlas_cell_height;
  uint32_t atlas_width;
  uint32_t atlas_height;
  uint32_t _pad1;             // 24
  uint32_t _pad2;             // 28
} Renderer_Info;

struct Renderer {
  SDL_Window *window;
  SDL_GLContext gl_context;

  Screen *screen;

  GLuint program;
  GLuint vbo;
  GLuint vao;
  GLuint instance_vbo;

  Renderer_Info info_ubo;
  GLuint info_ubo_id;

  GLuint atlas_texture;

  uint32_t current_width;
  uint32_t current_height;
};

/* Variables */
typedef uint32_t atlas_index_packed;

static Renderer_Cell glyth_buffer[GLYTH_BUFFER_MAX];
static size_t glyth_buffer_pos = 0;

static atlas_index_packed glyth_table[GLYTH_TABLE_MAX];
static uint32_t glyth_count = 0;
static Atlas atlas;
static GLint max_ubo_size = 0;
static GLint max_texture_size = 0;
static int ascent = 0; 
static int descent = 0;
static int line_gap = 0;
static float scale = 0.0;

/* Functions */
static bool renderer_init(Renderer*, Screen*);
static void renderer_destroy(Renderer*);
static void renderer_draw_screen(Renderer*);
static void renderer_resize_screen(Renderer*, int width, int height);

static void renderer_buffer_push(Terminal_Cell *cell, uint16_t x, uint16_t y);
static void renderer_buffer_sync();

char* slurp_file(const char * const src);
static bool screen_resize(Screen *s, uint32_t cols, uint32_t rows);
static bool compile_shader_source(const GLchar *source, GLenum shader_type, GLuint *shader);
static bool compile_shader_file(const char *file_path, GLenum shader_type, GLuint *shader);
static bool link_program(GLuint vert_shader, GLuint frag_shader, GLuint *program);

static bool glyth_table_init(const char *font_path, float pixel_height);
void glyth_table_destroy();

// void glyth_table_push(codepoint_t);

static atlas_index_packed glyth_table_get(codepoint_t);
void copy_bitmap_to_atlas(Atlas *atlas, int cell_x, int cell_y, const uint8_t *bitmap, int bw, int bh, int x0, int y0);
void dump_atlas_to_pgm(Atlas *atlas, const char *path);

static bool
renderer_init(Renderer *r, Screen *s) 
{
  *r = (Renderer) {0};
  r->screen = s;

  /* --- Create Window --- */
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    VTERROR("SDL_Init");
    _Exit(EXIT_FAILURE);
  }

  r->window = SDL_CreateWindow("vt", 800, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  r->current_width = 800;
  r->current_height = 600;

  /* --- Load OpenGL 3.3 --- */
  r->gl_context = SDL_GL_CreateContext(r->window);
  gladLoadGL();

  int major = 3, minor = 3;
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  VTINFO("OpenGL version %d.%d", major, minor);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  /* --- Compile and link shader program --- */
  GLuint vert_shader = 0;
  GLuint frag_shader = 0;
  if (!compile_shader_file("opengl/font.vert", GL_VERTEX_SHADER, &vert_shader)) {
    VTERROR("font.vert");
    renderer_destroy(r);
    return false;
  }

  if (!compile_shader_file("opengl/font.frag", GL_FRAGMENT_SHADER, &frag_shader)) {
    VTERROR("font.frag");
    renderer_destroy(r);
    return false;
  }

  if (!link_program(vert_shader,frag_shader, &r->program)) {
    VTERROR("gl_link_program");
    renderer_destroy(r);
    return false;
  }

  glUseProgram(r->program);

  /* --- Max ubo size and max texture size --- */
  glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &max_ubo_size);
  VTINFO("Max UBO size: %d bytes", max_ubo_size);
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
  VTINFO("Max texture size: %d x %d", max_texture_size, max_texture_size);

  /* --- bind vbo and vao --- */
  glGenVertexArrays(1, &r->vao); 
  glGenBuffers(1, &r->vbo);

  glBindVertexArray(r->vao);
  glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(Renderer_Cell), glyth_buffer, GL_DYNAMIC_DRAW);


  /* --- bind texture and create it --- */
  assert(r->atlas_texture == 0);
  glGenTextures(1, &r->atlas_texture);
  glBindTexture(GL_TEXTURE_2D, r->atlas_texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  if (!glyth_table_init(font_path, font_size_px)) { // defined in config.h
    return false;
  }

#ifdef DEBUG
  dump_atlas_to_pgm(&atlas, "atlas.pgm");
#endif

  /* --- create vbo for instancing and initialize attributes --- */
  glGenBuffers(1, &r->instance_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, r->instance_vbo);
  glBufferData(GL_ARRAY_BUFFER, GLYTH_BUFFER_MAX*sizeof(Renderer_Cell), glyth_buffer, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, r->instance_vbo);

  for (Renderer_Cell_Attr attr = 0; attr < ATTR_COUNT_RENDERER; attr++) {
    glEnableVertexAttribArray(attr);
    glVertexAttribIPointer(
        attr,
        1,
        GL_UNSIGNED_INT,
        sizeof(Renderer_Cell), 
        (void*) attr_offset_array[attr]);
    glVertexAttribDivisor(attr, 1); 
  }

  r->info_ubo = (Renderer_Info) {
    .atlas_width  = atlas.cell_width * atlas.cols,
    .atlas_height = atlas.cell_height * atlas.rows,
    .atlas_cell_width  = atlas.cell_width,
    .atlas_cell_height = atlas.cell_height,
    .grid_x = (float) r->current_width / atlas.cell_width,
    .grid_y = (float) r->current_height / atlas.cell_height,
  };

  glGenBuffers(1, &r->info_ubo_id);
  glBindBuffer(GL_UNIFORM_BUFFER, r->info_ubo_id);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(Renderer_Info), &r->info_ubo, GL_STATIC_DRAW);

  glBindBufferBase(GL_UNIFORM_BUFFER, 0, r->info_ubo_id);

  GLuint block_index = glGetUniformBlockIndex(r->program, "const_block");
  glUniformBlockBinding(r->program, block_index, 0);

  return true;
}

static void
renderer_destroy(Renderer *r) 
{
  if (!r) return;

  glyth_table_destroy();

  glDeleteProgram(r->program);
  if (r->gl_context) SDL_GL_DestroyContext(r->gl_context);
  if (r->window) SDL_DestroyWindow(r->window);
  memset(r, 0, sizeof(Renderer));
  SDL_Quit();
}

static void
renderer_draw_screen(Renderer *r) {
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  Screen screen = *(r->screen);

  Terminal_Cell *beg = screen.cell_buffer;
  Terminal_Cell *ptr = beg;
  Terminal_Cell *end = beg + screen.cols*screen.rows;
  
  for (; ptr < end; ptr++) {
    if(ptr->is_dirty) {
      size_t idx = ptr-beg;
      uint32_t x = idx % screen.cols;
      uint32_t y = idx / screen.cols;

      assert(x < screen.cols);

      renderer_buffer_push(ptr, x, y);
    }
  }

  renderer_buffer_sync();

  SDL_GL_SwapWindow(r->window);
}

static bool
screen_resize(Screen *s, uint32_t cols, uint32_t rows) 
{
  size_t old_len = ((uint32_t) s->cols) * ((uint32_t) s->rows);
  size_t new_len = cols * rows;
  if (new_len <= old_len) {
    s->cols = cols;
    s->rows = rows;
    VTDEBUG("Resize Screen = %ux%u", cols, rows);
    return true;
  }

  size_t type = sizeof(*s->cell_buffer);
  Terminal_Cell *new = realloc(s->cell_buffer, new_len * type);
  if (new) {
    s->cell_buffer = new;
    s->cols = cols;
    s->rows = rows;
    memset(&s->cell_buffer[old_len], 0, new_len-old_len);
    VTDEBUG("Resize Screen = %ux%u", cols, rows);
    return true;
  }

  VTWARN("Failed to resize screen!");
  return false;
}

void
renderer_resize_screen(Renderer *r, int width, int height) 
{
  r->current_width = width;
  r->current_height = height;
  glViewport(0, 0, width, height);

  float cols = (float) r->current_width / atlas.cell_width;
  float rows = (float) r->current_height / atlas.cell_height;

  VTDEBUG("Resize grid = %fx%f", cols, rows);

  screen_resize(r->screen, cols, rows);

  r->info_ubo.grid_x = cols;
  r->info_ubo.grid_y = rows;

  glBindBuffer(GL_UNIFORM_BUFFER, r->info_ubo_id);
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Renderer_Info), &r->info_ubo);
}

static void
renderer_buffer_push(Terminal_Cell *cell, uint16_t x, uint16_t y) 
{
  VTDEBUG("Renderer Push: %c -> %dx%d (Atlas idx = %u) ", (char) cell->codepoint, x,y, glyth_table_get(cell->codepoint));

  if (glyth_buffer_pos < GLYTH_BUFFER_MAX) {
    glyth_buffer[glyth_buffer_pos++] = (Renderer_Cell) {
      .pos = (x << 16) | (y),
      .glyth_index = glyth_table_get(cell->codepoint),
      .foreground = cell->fg,
      .background = cell->bg,
    };
  }
}

static void
renderer_buffer_sync() {

  VTDEBUG("Drawing %ld glyths", glyth_buffer_pos);

  assert(glyth_buffer_pos < GLYTH_BUFFER_MAX);
  glBufferSubData(GL_ARRAY_BUFFER, 0, glyth_buffer_pos * sizeof(Renderer_Cell), glyth_buffer);
  glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, glyth_buffer_pos);
  glyth_buffer_pos = 0;
}

void
copy_bitmap_to_atlas(
    Atlas *atlas,
    int cell_x, int cell_y,           // row# and col# in atlas
    const uint8_t *bitmap,            // glyph bitmap 
    int bw, int bh,                   // bitmap width and height
    int x0, int y0                    // glyph origin offsets 
    )
{

  int atlas_width_px  = atlas->cell_width  * atlas->cols;
  int atlas_height_px = atlas->cell_height * atlas->rows;

  int dst_cell_x_px = cell_x * atlas->cell_width;
  int dst_cell_y_px = cell_y * atlas->cell_height;

  int ascent_px = scale * ascent;

  for (int row = 0; row < bh; row++) {
    for (int col = 0; col < bw; col++) {
      int dst_x = dst_cell_x_px + col + x0;
      int dst_y = dst_cell_y_px + ascent_px + y0 + row;

      if (dst_x >= 0 && dst_x < atlas_width_px &&
          dst_y >= 0 && dst_y < atlas_height_px) {
        atlas->atlas[dst_y * atlas_width_px + dst_x] =
          bitmap[row * bw + col];
      }
    }
  }
}

void
dump_atlas_to_pgm(Atlas *atlas, const char *path)
{

  unsigned char *data = (unsigned char*) atlas->atlas;
  uint32_t width = atlas->cell_width * atlas->cols;
  uint32_t height = atlas->cell_height * atlas->rows;

  FILE *f = fopen(path, "wb");
  if (!f) {
    VTERROR("fopen");
    return;
  }

  fprintf(f, "P5\n%d %d\n255\n", width, height);  // PGM header
  fwrite(data, 1, width * height, f);
  fclose(f);
}

static atlas_index_packed
glyth_table_get(codepoint_t codepoint)
{
  /* Reserved Drawable ASCII Range */

  if (32 <= codepoint && codepoint <= 128) {
    uint32_t idx = codepoint - 32;
    uint32_t x = (idx % atlas.cols) << 16;
    uint32_t y = (idx / atlas.cols);
    return x|y;
  }

  // TODO: get glyths from hash table
  // if not present add them

  return 0;
}


static bool
glyth_table_init(const char *font_path, float pixel_height)
{
  /* font metrics part, the cell width and height fixed and derived from the font,
   * addicionally each cell of the texture should be equivalent to the full size of a terminal
   * meaning it also has padding for any ascent descent that might be needed
   *
   * while this wastes precious texture size, it avoids sending and updating a
   * lot of data as well, and uv coords are simplified to an integer */

  stbtt_fontinfo font;
  unsigned char *ttf_buffer = (unsigned char*) slurp_file(font_path);
  if (!stbtt_InitFont(&font, ttf_buffer, 0)) {
    VTERROR("stbtt_InitFont");
    free(ttf_buffer);
    return false;
  }

  scale = stbtt_ScaleForPixelHeight(&font, pixel_height);
  stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);

  uint32_t cell_height = (int)(scale * (ascent - descent + line_gap));

  /* ascii printable range */
  int max_advance = 0;
  for (int i = 32; i < 128; i++) { 
    int ax;
    stbtt_GetCodepointHMetrics(&font, i, &ax, 0);
    if (ax > max_advance) max_advance = ax;
  }

  int cell_width = (int)(scale * max_advance);

  VTDEBUG("atlas_cell = %dx%d", cell_width, cell_height);

  /* glyth table initialization */
  Atlas new_atlas = {
    .texture = 0,   // no texture yet
    .atlas = NULL,
    .cell_height = cell_height,
    .cell_width = cell_width,
    .rows = ATLAS_COLS,
    .cols = ATLAS_ROWS,
  };

  size_t atlas_width_px  = new_atlas.cell_width  * new_atlas.cols;
  size_t atlas_height_px = new_atlas.cell_height * new_atlas.rows;
  VTDEBUG("texture_size = %ld x %ld", atlas_width_px, atlas_height_px);

  if ( (atlas_width_px > (size_t) max_texture_size)
    || (atlas_height_px > (size_t) max_texture_size) ) {
    VTERROR("texture size is to large");
    free(ttf_buffer);
    return false;
  }

  new_atlas.atlas = calloc(atlas_width_px * atlas_height_px, 1);
  for (unsigned char c = 32; c < 128; c++) {

    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&font, c, scale, scale, &x0, &y0, &x1, &y1);

    int bw = x1 - x0;
    int bh = y1 - y0;

    if (bw == 0 || bh == 0) continue;
    assert(bw > 0 && bh > 0);

    uint8_t *bitmap = malloc(bw * bh);
    stbtt_MakeCodepointBitmap(&font, bitmap, bw, bh, bw, scale, scale, c);

    int glyph_index = c - 32;
    int cell_x = glyph_index % new_atlas.cols;
    int cell_y = glyph_index / new_atlas.cols;

    copy_bitmap_to_atlas(&new_atlas, cell_x, cell_y, bitmap, bw, bh, x0, y0);
    free(bitmap);
    glyth_count++;
  }


  atlas = new_atlas;
  free(ttf_buffer);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlas_width_px , atlas_height_px, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.atlas);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  return true;
}

void
glyth_table_destroy()
{
  if(atlas.atlas) { free(atlas.atlas); }
}

char *
slurp_file(const char * const src)
{
  int fd = open(src, O_RDONLY, 0644);
  if (fd < 0) return NULL;

  size_t len = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, 0);

  char *retv = (char*) malloc(len+1);
  read(fd, retv, len);
  retv[len] = '\0';
  return retv;
}

static bool
compile_shader_source(const GLchar *source, GLenum shader_type, GLuint *shader) 
{
  *shader = glCreateShader(shader_type);
  glShaderSource(*shader, 1, &source, NULL);
  glCompileShader(*shader);

  GLint gl_compiled = 0;
  glGetShaderiv(*shader, GL_COMPILE_STATUS, &gl_compiled);

  if (!gl_compiled) {
    GLchar message[1024];
    GLsizei message_size = 0;
    glGetShaderInfoLog(*shader, sizeof(message), &message_size, message);
    VTERROR("could not gl_compile!");
    fprintf(stderr, "%.*s", message_size, message);
    return false;
  }

  return true;
}

static bool
compile_shader_file(const char *file_path, GLenum shader_type, GLuint *shader) 
{
  char *source = slurp_file(file_path);
  if (source == NULL) {
    VTERROR("ERROR: failed to read file `%s`: %d", file_path, errno);
    return false;
  }
  bool ok = compile_shader_source(source, shader_type, shader);
  if (!ok) {
    VTERROR("ERROR: failed to gl_compile `%s` shader file", file_path);
    return false;
  }
  free(source);
  return ok;
}

static bool
link_program(GLuint vert_shader, GLuint frag_shader, GLuint *program) 
{
  *program = glCreateProgram();

  glAttachShader(*program, vert_shader);
  glAttachShader(*program, frag_shader);
  glLinkProgram(*program);

  GLint linked = 0;
  glGetProgramiv(*program, GL_LINK_STATUS, &linked);
  if (!linked) {
    GLsizei message_size = 0;
    GLchar message[1024];

    glGetProgramInfoLog(*program, sizeof(message), &message_size, message);
    VTERROR("Program Linking: %.*s", message_size, message);
  }

  glDeleteShader(vert_shader);
  glDeleteShader(frag_shader);
  return program;
}
