#pragma once
#include "vt_renderer.h"
#include "config.h"

/* For the OpenGL3 renderer a lot of the modern features are either not available 
 * or not exposed by the drivers (thanks a lot intel)
 *
 * So all visible terminal cells must be sent to the GPU each frame instead of updating
 * only the dirty cells stored in an SSBO
 *
 * Having the terminal cells as a UBO would limit most hardware to around 5000
 * cells, reasonable for a laptop maybe, but not enough even for medium sized monitors
 */

/* 900 glyths in the texture at any time
 * should be more than enough for most cases */
#define ATLAS_ROWS 30
#define ATLAS_COLS 30
#define GLYTH_TABLE_MAX (ATLAS_COLS * ATLAS_ROWS)

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

struct Renderer {

  Renderer_Cell *cell_buffer;
  SDL_Window *window;

  SDL_GLContext gl_context;

  GLuint program;

  GLuint const_ubo;
  GLuint render_texture;

  uint32_t current_width;
  uint32_t current_height;
};

/* Variables */
typedef uint32_t atlas_index_packed;
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
bool glyth_table_init(const char *font_path, float pixel_height);
void glyth_table_shutdown();
void copy_bitmap_to_atlas(Atlas *atlas, int cell_x, int cell_y,
    const uint8_t *bitmap, int bw, int bh, int x0, int y0);
void dump_atlas_to_pgm(Atlas *atlas, const char *path);

atlas_index_packed glyth_table_get(codepoint_t);
void glyth_table_push(codepoint_t);
char* slurp_file(const char * const src);
bool gl_compile_shader_source(const GLchar *source, GLenum shader_type, GLuint *shader);
bool gl_compile_shader_file(const char *file_path, GLenum shader_type, GLuint *shader);
bool gl_link_program(GLuint vert_shader, GLuint frag_shader, GLuint *program);

void copy_bitmap_to_atlas(
    Atlas *atlas,
    int cell_x, int cell_y,           // Cell index in atlas (not pixel coords)
    const uint8_t *bitmap,            // Glyph bitmap (grayscale)
    int bw, int bh,                   // Bitmap width and height
    int x0, int y0                    // Glyph origin offsets (from stbtt_GetCodepointBitmapBox)
    ) {

  int atlas_width_px  = atlas->cell_width  * atlas->cols;
  int atlas_height_px = atlas->cell_height * atlas->rows;

  int dst_cell_x_px = cell_x * atlas->cell_width;
  int dst_cell_y_px = cell_y * atlas->cell_height;
#ifdef DEBUG
  printf("Copyting to %dx%d\n", dst_cell_x_px, dst_cell_y_px );
#endif

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

void dump_atlas_to_pgm(Atlas *atlas, const char *path) {

  unsigned char *data = (unsigned char*) atlas->atlas;
  uint32_t width = atlas->cell_width * atlas->cols;
  uint32_t height = atlas->cell_height * atlas->rows;
  printf("atlas: %d x %d\n", width, height);

  FILE *f = fopen(path, "wb");
  if (!f) {
    perror("fopen");
    return;
  }

  fprintf(f, "P5\n%d %d\n255\n", width, height);  // PGM header
  fwrite(data, 1, width * height, f);
  fclose(f);

}


bool glyth_table_init(const char *font_path, float pixel_height) {

  /* font metrics part, the cell width and height fixed and derived from the font,
   * addicionally each cell of the texture should be equivalent to the full size of a terminal
   * meaning it also has padding for any ascent descent that might be needed
   *
   * while this wastes precious texture size, it avoids sending and updating a
   * lot of data as well, and uv coords are simplified to an integer */

  stbtt_fontinfo font;
  unsigned char *ttf_buffer = (unsigned char*) slurp_file(font_path);
  if (!stbtt_InitFont(&font, ttf_buffer, 0)) {
    perror("stbtt_InitFont");
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
#ifdef DEBUG
  printf("atlas_cell = %dx%d\n", cell_width, cell_height);
#endif

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
  printf("texture_size = %ld x %ld\n", atlas_width_px, atlas_height_px);

  if ( (atlas_width_px > (size_t) max_texture_size)
    || (atlas_height_px > (size_t) max_texture_size) ) {
    perror("texture size is to large");
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
#ifdef DEBUG
    printf("writing %c to %dx%d\n", c, cell_x, cell_y);
#endif

    copy_bitmap_to_atlas(&new_atlas, cell_x, cell_y, bitmap, bw, bh, x0, y0);
    free(bitmap);
    glyth_count++;
  }

  atlas = new_atlas;

  free(ttf_buffer);
  return true;
}

void glyth_table_destroy() {
  if(atlas.atlas) { free(atlas.atlas); }
}

bool renderer_create(Renderer *r) {
  *r = (Renderer) {0};

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    perror("SDL_Init");
    _Exit(EXIT_FAILURE);
  }

  r->window = SDL_CreateWindow("vt", 800, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  r->current_width = 800;
  r->current_height = 600;
  r->gl_context = SDL_GL_CreateContext(r->window);

  gladLoadGL();

  int major = 3, minor = 3;
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  printf("OpenGL version %d.%d\n", major, minor);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  GLuint vert_shader = 0;
  GLuint frag_shader = 0;
  if (!gl_compile_shader_file("opengl/font.vert", GL_VERTEX_SHADER, &vert_shader)) {
    perror("font.vert");
    renderer_destroy(r);
    return false;
  }

  if (!gl_compile_shader_file("opengl/font.frag", GL_FRAGMENT_SHADER, &frag_shader)) {
    perror("font.frag");
    renderer_destroy(r);
    return false;
  }

  if (!gl_link_program(vert_shader,frag_shader, &r->program)) {
    perror("gl_link_program");
    renderer_destroy(r);
    return false;
  }

  glUseProgram(r->program);

  glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &max_ubo_size);
  printf("Max UBO size: %d bytes\n", max_ubo_size);
  // r->max_glyth_count = max_ubo_size / sizeof(Renderer_Cell);
  // printf("Max Glyth Count: %d\n", r->max_glyth_count);

  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
  printf("Max texture size: %d x %d\n", max_texture_size, max_texture_size);

  /* Create the texture and glyth table */
  if (!glyth_table_init(font_path, font_size_px)) { // from config.h
    return false;
  }

#ifdef DEBUG
  dump_atlas_to_pgm(&atlas, "atlas.pgm");
#endif

  return true;
}

void renderer_destroy(Renderer *r) {
  if (!r) return;

  glDeleteProgram(r->program);
  if (r->gl_context) SDL_GL_DestroyContext(r->gl_context);
  if (r->window) SDL_DestroyWindow(r->window);
  memset(r, 0, sizeof(Renderer));
  SDL_Quit();
}

char *slurp_file(const char * const src) {
  int fd = open(src, O_RDONLY, 0644);
  if (fd < 0) return NULL;

  size_t len = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, 0);

  char *retv = (char*) malloc(len+1);
  read(fd, retv, len);
  retv[len] = '\0';
  return retv;
}

bool gl_compile_shader_source(const GLchar *source, GLenum shader_type, GLuint *shader) {
  *shader = glCreateShader(shader_type);
  glShaderSource(*shader, 1, &source, NULL);
  glCompileShader(*shader);

  GLint gl_compiled = 0;
  glGetShaderiv(*shader, GL_COMPILE_STATUS, &gl_compiled);

  if (!gl_compiled) {
    GLchar message[1024];
    GLsizei message_size = 0;
    glGetShaderInfoLog(*shader, sizeof(message), &message_size, message);
    fprintf(stderr, "ERROR: could not gl_compile!\n");
    fprintf(stderr, "%.*s\n", message_size, message);
    return false;
  }

  return true;
}

bool gl_compile_shader_file(const char *file_path, GLenum shader_type, GLuint *shader) {
  char *source = slurp_file(file_path);
  if (source == NULL) {
    fprintf(stderr, "ERROR: failed to read file `%s`: %d\n", file_path, errno);
    errno = 0;
    return false;
  }
  bool ok = gl_compile_shader_source(source, shader_type, shader);
  if (!ok) {
    fprintf(stderr, "ERROR: failed to gl_compile `%s` shader file\n", file_path);
  }
  free(source);
  return ok;
}

bool gl_link_program(GLuint vert_shader, GLuint frag_shader, GLuint *program) {
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
    fprintf(stderr, "Program Linking: %.*s\n", message_size, message);
  }

  glDeleteShader(vert_shader);
  glDeleteShader(frag_shader);
  return program;
}
