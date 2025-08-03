#pragma once
#include "vt_opengl.h"

#define MIN(a,b) ((a < b) ? a : b)
#define GLYTH_BUF_SIZ 4024
#define CHAR_COUNT 127

#define X 0
#define Y 1

#define FIRST_CHAR 0
#define LAST_CHAR 127
#define FONT_SIZE 64.0f
#define ATLAS_WIDTH 512
#define ATLAS_HEIGHT 512

/* Check this out:
 * -> https://github.com/tsoding/opengl-template
 */

typedef struct {
  size_t offset;
  size_t elems;
} Renderer_Cell_Attr_Def;


static int descent, ascent, line_gap;
static float scale;

static const Renderer_Cell_Attr_Def cell_attr_array[ATTR_CELL_COUNT] = {
  [CELL_ATTR_POS]   = { offsetof(Renderer_Cell, pos),  4 },
  [CELL_ATTR_UV]    = { offsetof(Renderer_Cell, uv),   4 },
  [CELL_ATTR_FG]    = { offsetof(Renderer_Cell, fg),   4 },
  [CELL_ATTR_BG]    = { offsetof(Renderer_Cell, bg),   4 },
};

static_assert(ATTR_CELL_COUNT == 4, "Renderer_Cell attributes has changed, update offset array.");


Renderer Renderer_Create(const char * const font_path, size_t font_height, uint32_t screen_width, uint32_t screen_height);
void Renderer_Destroy(Renderer *renderer);
void Renderer_SetFont(Renderer *renderer, const char * const src);
void Renderer_ResizeScreen(Renderer *renderer, uint32_t width, uint32_t height);
void Renderer_Draw(Renderer *renderer);
void Renderer_Push(Renderer *renderer, Renderer_Cell cell);

/* helpers */
static char *slurp_file(const char * const src);
static bool compile_shader_source(const GLchar *source, GLenum shader_type, GLuint *shader);
static bool compile_shader_file(const char *file_path, GLenum shader_type, GLuint *shader);
static bool link_program(GLuint vert_shader, GLuint frag_shader, GLuint *program);

static float renderer_get_colsf(Renderer *renderer);
static int   renderer_get_colsi(Renderer *renderer);
static float renderer_get_rowsf(Renderer *renderer);
static int   renderer_get_rowsi(Renderer *renderer);
static uint32_t renderer_cell_buffer_size(Renderer *renderer);

static char
*slurp_file(const char * const src) {

  int fd = open(src, O_RDONLY, 0644);
  if (fd < 0) return NULL;

  size_t len = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, 0);

  /* we have to have some limit */
  assert(len <= (1024*1024 * 100)); 

  char *retv = (char*) malloc(len+1);
  read(fd, retv, len);
  retv[len] = '\0';
  return retv;
}

static bool
compile_shader_source(const GLchar *source, GLenum shader_type, GLuint *shader) {
    *shader = glCreateShader(shader_type);
    glShaderSource(*shader, 1, &source, NULL);
    glCompileShader(*shader);

    GLint compiled = 0;
    glGetShaderiv(*shader, GL_COMPILE_STATUS, &compiled);

    if (!compiled) {
        GLchar message[1024];
        GLsizei message_size = 0;
        glGetShaderInfoLog(*shader, sizeof(message), &message_size, message);
        fprintf(stderr, "ERROR: could not compile!\n");
        fprintf(stderr, "%.*s\n", message_size, message);
        return false;
    }

    return true;
}

static bool
compile_shader_file(const char *file_path, GLenum shader_type, GLuint *shader) {
    char *source = slurp_file(file_path);
    if (source == NULL) {
        fprintf(stderr, "ERROR: failed to read file `%s`: %d\n", file_path, errno);
        errno = 0;
        return false;
    }
    bool ok = compile_shader_source(source, shader_type, shader);
    if (!ok) {
        fprintf(stderr, "ERROR: failed to compile `%s` shader file\n", file_path);
    }
    free(source);
    return ok;
}

static bool
link_program(GLuint vert_shader, GLuint frag_shader, GLuint *program) {
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

/* You normally wouldn't need getters here
 * but it's important to remember that cols and rows
 * can be floats or ints */
static float renderer_get_rowsf(Renderer *renderer) {
  return (float) renderer->screen_size[Y] / renderer->cell_size[Y];
}

static int renderer_get_rowsi(Renderer *renderer) {
  return (int) renderer->screen_size[Y] / renderer->cell_size[Y];
}
static float renderer_get_colsf(Renderer *renderer) {
  return (float) renderer->screen_size[X] / renderer->cell_size[X];
}
static int renderer_get_colsi(Renderer *renderer) {
  return (int) renderer->screen_size[X] / renderer->cell_size[X];
}

static uint32_t renderer_cell_buffer_size(Renderer *renderer) {
  return renderer_get_colsi(renderer) * renderer_get_rowsi(renderer);
}

Renderer
Renderer_Create(const char * const font_path, size_t font_height, uint32_t screen_width, uint32_t screen_height) {

  Renderer new = {0};

  if (!compile_shader_file("font.vert", GL_VERTEX_SHADER, &new.vert_shader)) {
    perror("font.vert");
    _Exit(EXIT_FAILURE);
  }

  if (!compile_shader_file("font.frag", GL_FRAGMENT_SHADER, &new.frag_shader)) {
    perror("font.frag");
    _Exit(EXIT_FAILURE);
  }

  if (!link_program(new.vert_shader,new.frag_shader, &new.program)) {
    perror("link_program");
    _Exit(EXIT_FAILURE);
  }

  /* the idea was to make one of those const arrays but its a bit overkill for now */
  new.uniforms[RENDERER_UNIFORM_GRID]  = glGetUniformLocation(new.program, "grid");
  new.uniforms[RENDERER_UNIFORM_ATLAS] = glGetUniformLocation(new.program, "atlas");
  new.uniforms[RENDERER_UNIFORM_PASS]  = glGetUniformLocation(new.program, "renderingPass");

  glUseProgram(new.program);

  glUniform2f(new.uniforms[RENDERER_UNIFORM_GRID], 0, 0);
  glUniform2f(new.uniforms[RENDERER_UNIFORM_ATLAS], 0, 0);
  glUniform1i(new.uniforms[RENDERER_UNIFORM_PASS], RENDER_PASS_BACKGROUND);

  /* Set font before creating buffer so we can know cell size */
  new.cell_size[Y] = font_height;
  Renderer_SetFont(&new, font_path);
  Renderer_ResizeScreen(&new, screen_width, screen_height);

  /* Vertex buffers */
  glGenVertexArrays(1, &new.vao); 
  glGenBuffers(1, &new.vbo);
  glBindVertexArray(new.vao);
  glBindBuffer(GL_ARRAY_BUFFER, new.vbo);

  /* Max Buffer Data Cell Size */
  glBufferData( GL_ARRAY_BUFFER, sizeof(Renderer_Cell) * CELL_BUFFER_MAX, new.cell_buffer, GL_DYNAMIC_DRAW);

  for (Renderer_Cell_Attr attr = 0; attr < ATTR_CELL_COUNT; attr++) {
    Renderer_Cell_Attr_Def def = cell_attr_array[attr];
    glEnableVertexAttribArray(attr);
    glVertexAttribPointer(
        attr,
        def.elems,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Renderer_Cell), 
        (void*) def.offset);
    /* We have these attributes once per instance */
    glVertexAttribDivisor(attr, 1); 
  }

  return new;
}

void
Renderer_Destroy(Renderer *renderer) {
  // if (renderer->cell_buffer) free(renderer->cell_buffer);
  glDeleteVertexArrays(1, &renderer->vao);
  glDeleteBuffers(1, &renderer->vbo);
  glDeleteProgram(renderer->program);
  memset(renderer, 0, sizeof(*renderer));
}

void
Renderer_SetFont(Renderer *renderer, const char * const src) {

  unsigned char *font_buffer = (unsigned char*) slurp_file(src);
  unsigned char *bitmap = calloc(ATLAS_WIDTH * ATLAS_HEIGHT, 1);

  stbtt_InitFont(&renderer->font_info, font_buffer, stbtt_GetFontOffsetForIndex(font_buffer, 0));

  stbtt_bakedchar cdata[LAST_CHAR-FIRST_CHAR];
  stbtt_BakeFontBitmap(font_buffer, 0, renderer->cell_size[Y], bitmap, ATLAS_WIDTH, ATLAS_HEIGHT, FIRST_CHAR, CHAR_COUNT, cdata);

  for (int i = 0; i < CHAR_COUNT; i++) {
    stbtt_bakedchar* g = &cdata[i];

    renderer->glyth_table_ascii[i] = (Renderer_Character){
      .size = { (int)(g->x1 - g->x0), (int)(g->y1 - g->y0) },
        .bearing = { (int)(g->xoff),       (int)(g->yoff) },
        .advance = g->xadvance,
        .uv_min  = { g->x0 / (float)ATLAS_WIDTH, g->y0 / (float)ATLAS_HEIGHT },
        .uv_max  = { g->x1 / (float)ATLAS_WIDTH, g->y1 / (float)ATLAS_HEIGHT },
    };
    
  }

  glGenTextures(1, &renderer->atlas_texture);
  glBindTexture(GL_TEXTURE_2D, renderer->atlas_texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, ATLAS_WIDTH, ATLAS_HEIGHT, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  // do i even need a uniform for the atlas
  glUniform1i(renderer->uniforms[RENDERER_UNIFORM_ATLAS], 0); 

  assert(renderer->cell_size[1] > 0);

  stbtt_GetFontVMetrics(&renderer->font_info, &ascent, &descent, &line_gap);
  scale = stbtt_ScaleForPixelHeight(&renderer->font_info, renderer->cell_size[1]);

  renderer->cell_size[0] = renderer->glyth_table_ascii[(int)'W'].size.x;

  free(bitmap);
  free(font_buffer);
}

void
Renderer_ResizeScreen(Renderer *renderer, uint32_t width, uint32_t height) {

  assert(renderer->cell_size[0] > 0 && renderer->cell_size[1] > 0);
  renderer->screen_size[X] = width;
  renderer->screen_size[Y] = height;

  float cols = renderer_get_colsf(renderer);
  float rows = renderer_get_rowsf(renderer);

  glViewport(0, 0, width, height);
  glUniform2f(renderer->uniforms[RENDERER_UNIFORM_GRID], cols, rows);
}

void
Renderer_Draw(Renderer *renderer) {

  glBufferSubData(GL_ARRAY_BUFFER, 0, renderer->cell_buffer_pos * sizeof(Renderer_Cell), renderer->cell_buffer);

  glUniform1i(renderer->uniforms[RENDERER_UNIFORM_PASS], RENDER_PASS_BACKGROUND);
  glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, renderer->cell_buffer_pos);

  glUniform1i(renderer->uniforms[RENDERER_UNIFORM_PASS], RENDER_PASS_GLYTHS);
  glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, renderer->cell_buffer_pos);

  renderer->cell_buffer_pos = 0;
}

void
Renderer_Push(Renderer *renderer, Renderer_Cell cell) {
  renderer->cell_buffer[renderer->cell_buffer_pos++] = cell;
  assert(renderer->cell_buffer_pos < CELL_BUFFER_MAX);
}
