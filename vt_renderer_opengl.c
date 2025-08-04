#pragma once

#include <stdlib.h>
#include <unistd.h>
typedef struct {
  uint32_t cell_size[2];
  uint32_t term_size[2];
  uint32_t top_left_margin[2];
  uint32_t blink_modulate;
  uint32_t margin_color;
  uint32_t strike_min;
  uint32_t strike_max;
  uint32_t underline_min;
  uint32_t underline_max;
} Renderer_Const_Buffer;

#define RENDERER_CELL_BLINK 0x80000000

typedef struct {
  uint32_t glyth_index;
  uint32_t foreground;
  uint32_t background;
} Renderer_Cell;

typedef struct {

  Renderer_Cell *cell_buffer;

  SDL_Window *window;
  SDL_GLContext gl_context;

  GLuint vertex_program;
  GLuint pixel_program;
  GLuint compute_program;

  GLuint const_ubo; 

  // GLuint framebuffer;
  // GLuint render_texture;
  // GLuint depth_stencil_renderbuffer;
  //
  // GLuint render_ssbo;
  //
  // GLuint cell_ssbo;
  // GLuint cell_texture;
  // GLuint cell_texture_unit;
  //
  // GLuint glyth_texture;
  // GLuint glyth_sampler;
  //
  // GLuint glyth_transfer_texture;
  // GLuint glyth_transfer_pbo;

  uint32_t current_width;
  uint32_t current_height;
  uint32_t max_cell_count;
} Renderer;


Renderer renderer_create();

void renderer_destroy(Renderer *r);

Renderer renderer_create() {
  Renderer r = {0};

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    perror("SDL_Init");
    _Exit(EXIT_FAILURE);
  }

  r.window = SDL_CreateWindow("vt", 800, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  r.current_width = 800;
  r.current_height = 600;
  gladLoadGL();

  int major = 4, minor = 2;
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  printf("OpenGL version %d.%d\n", major, minor);

  r.gl_context = SDL_GL_CreateContext(r.window);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  // SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  // SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  if (!r.window) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return r;
  }

  r.gl_context = SDL_GL_CreateContext(r.window);
  if (!r.gl_context) {
    fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
    return r;
  }

  // SDL_GL_SetSwapInterval(1);

  r.max_cell_count = 0;


  return r;
}

void renderer_cell_buffer_destroy() {

}

void renderer_destroy(Renderer *r) {
  if (!r) return;

  if (r->gl_context) {
    SDL_GL_DestroyContext(r->gl_context);
  }

  if (r->window) {
    SDL_DestroyWindow(r->window);
  }

  memset(r, 0, sizeof(Renderer);

  SDL_Quit();
}

