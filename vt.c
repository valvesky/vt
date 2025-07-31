#include "vt_circ_buf.c"

#include "lib/glad.c"

/* maybe switch to stb in the future */
#define STB_TRUETYPE_IMPLEMENTATION
#include "lib/stb_truetype.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "vt_vec.c"
#include "vt_opengl.c"
#include "vt_term.c"

#define GLYTH_BUF_SIZ 1024
#define CHAR_COUNT 128

#define FIRST_CHAR 0
#define LAST_CHAR 127
#define FONT_SIZE 64.0f
#define ATLAS_WIDTH 512
#define ATLAS_HEIGHT 512

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Event event;

typedef struct {
  vec2i size;     
  vec2i bearing;
  float advance;
  vec2f uv_min;
  vec2f uv_max;
} Character;

typedef struct {
  vec4f pos;
  vec4f uv;
  vec4f fg;
  vec4f bg;
} Cell;

typedef enum {
  CELL_ATTR_POS = 0,
  CELL_ATTR_UV,
  CELL_ATTR_FG,
  CELL_ATTR_BG,
  ATTR_CELL_COUNT
} Cell_Attr;

typedef struct {
  size_t offset;
  size_t elems;
} Cell_Attr_Def;

static const Cell_Attr_Def cell_attr_array[ATTR_CELL_COUNT] = {
  [CELL_ATTR_POS]   = { offsetof(Cell, pos),  4 },
  [CELL_ATTR_UV]    = { offsetof(Cell, uv),   4 },
  [CELL_ATTR_FG]    = { offsetof(Cell, fg),   4 },
  [CELL_ATTR_BG]    = { offsetof(Cell, bg),   4 },
};

#define RENDER_PASS_BG 0
#define RENDER_PASS_GLYTHS 1

static_assert(ATTR_CELL_COUNT == 4, "Cell attributes has changed, update offset array.");

static Character char_table[128]; // contains glyth information for each character
static Cell cell_buffer[GLYTH_BUF_SIZ]; // instanced cells to be rendered
static size_t cell_buffer_pos = 0;

static GLuint atlas_texture = 0; 
static GLuint atlas_uniform = 0;
static GLuint grid_uniform = 0;
static GLuint rendering_pass = 0;

static int ascent = 0;
static int descent = 0;
static int line_gap = 0;
static float scale = 0;

static uint64_t frames = 0;
static bool running = true;
static bool redraw = true;
static bool cursor_inverted = true;
static Term vt;

static void UploadAtlasAndPopulateCharTable(const char * const src);
static Character GetCharacter(int c);
static void CellBufferPush(Cell new);
static void CellBufferRender();

static void UI_RenderTerm(term_t vt);
static void UI_RenderText(char *text, size_t len, vec2f pos, vec4f color);

static void
UploadAtlasAndPopulateCharTable(const char * const src) {

  char *font_buffer = slurp_file(src);
  char *bitmap = calloc(ATLAS_WIDTH * ATLAS_HEIGHT, 1);

  stbtt_fontinfo font;
  stbtt_InitFont(&font, font_buffer, stbtt_GetFontOffsetForIndex(font_buffer, 0));

  stbtt_bakedchar cdata[128];
  stbtt_BakeFontBitmap(font_buffer, 0, FONT_SIZE, bitmap, ATLAS_WIDTH, ATLAS_HEIGHT, FIRST_CHAR, CHAR_COUNT, cdata);
  stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
  scale = stbtt_ScaleForPixelHeight(&font, FONT_SIZE);
  
  for (int i = 0; i < CHAR_COUNT; i++) {
    stbtt_bakedchar* g = &cdata[i];

    char_table[i] = (Character){
      .size    = { (int)(g->x1 - g->x0), (int)(g->y1 - g->y0) },
        .bearing = { (int)(g->xoff),       (int)(g->yoff) },
        .advance = g->xadvance,
        .uv_min  = { g->x0 / (float)ATLAS_WIDTH, g->y0 / (float)ATLAS_HEIGHT },
        .uv_max  = { g->x1 / (float)ATLAS_WIDTH, g->y1 / (float)ATLAS_HEIGHT },
    };
    // printf("%c -> %d %d\n", (char) i, char_table[i].size.x, char_table[i].size.y);
  }

  glGenTextures(1, &atlas_texture);
  glBindTexture(GL_TEXTURE_2D, atlas_texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, ATLAS_WIDTH, ATLAS_HEIGHT, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glUniform1i(atlas_uniform, 0);

  free(bitmap);
  free(font_buffer);
}

static Character
GetCharacter(int c) {
  if (c >= 128) c = '?';
  return char_table[c]; 
}

static void
CellBufferPush(Cell new) {
  assert(cell_buffer_pos < GLYTH_BUF_SIZ); 
  cell_buffer[cell_buffer_pos++] = new;
}

static void
CellBufferRender() {

  glBufferSubData(GL_ARRAY_BUFFER, 0, cell_buffer_pos * sizeof(Cell), cell_buffer);

  glUniform1i(rendering_pass, RENDER_PASS_BG);
  glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, cell_buffer_pos);

  glUniform1i(rendering_pass, RENDER_PASS_GLYTHS);
  glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, cell_buffer_pos);
  
}

static void
UI_RenderTerm(term_t term) {

  // puts("--- Render Term ---");
  for (int i = 0; i <= LL_COUNT(term); i++) {
    LogicalLine ll = term_ll_get_last(term, i);
    char *ptr = term_scrollback_get(term, ll.start);
    // printf("ll = %ld %ld\n", ll.start, ll.len);
    // printf("string = %*s\n", ll.len, ptr);
    UI_RenderText(ptr, ll.len, (vec2f) {0, i}, term->draw_state.fg);
  }
  
  // LogicalLine cmd = term_ll_get_nonterminated(&vt);
  // UI_RenderText(vt.scrollback.buffer+cmd.start, cmd.len+vt.cmd_len, (vec2f) {0, 1}, color);

  // if (vt.cursor == vt.cmd_len) {
  //   Cell new = {
  //     .pos = { pos.x, (ROWS-pos.y-1), 0, 0},
  //     .uv  = { 0, 0, 0, 0},
  //     .fg  = color,
  //     .bg  = { 0.0, 0.0, 0.0, 1.0},
  //   };
  //   if (cursor_inverted) {
  //     vec4f temp = new.bg;
  //     new.bg = new.fg;
  //     new.fg = temp;
  //   }
  //   CellBufferPush(new);
  // }

  CellBufferRender();
  cell_buffer_pos = 0;
}

static void
UI_RenderText(char *text, size_t len, vec2f pos, vec4f color) {

  for (size_t i = 0; i < len; i++) {
    /* End of logical line */
    if (text[i] == '\n') {
      continue;
    }
    Character ch = GetCharacter((int)text[i]);

    /*  The point we use to draw the rectangle could theoretically be a single
     *  index that we use to get the grid cell but we need to offset the char
     *  according to it's bearing.
     *
     *  Glyth position    Atlas uv coords
     *    |   
     *  H |              +--------uv_max
     *    |              |           |
     *    P------        uv_min -----|
     *       W
     */

    float asc      = (ascent * scale) / cell_dim.y;
    float bearingY = ((float)ch.bearing.y) / cell_dim.y;
    float h        = ((float)ch.size.y) / cell_dim.y;
    float w        = (float)ch.size.x / cell_dim.x;

    float x = pos.x + ((1-w)/2);
    float y = pos.y + (asc - (bearingY + h));

    Cell new = {
      .pos = { x, y, w, h},
      .uv  = { ch.uv_max.x, ch.uv_max.y, ch.uv_min.x, ch.uv_min.y},
      .fg  = color,
      .bg  = { 0.0, 0.0, 0.0, 1.0},
    };

    // if (i == vt.cursor && cursor_inverted) {
    //   new.fg = new.bg;
    //   new.bg = color;
    // }

    CellBufferPush(new);

    pos.x += 1;
    if (pos.x+1 > (int) COLS) {
      pos.y--;
      pos.x = 0;
    }
  }
}

int main() {

  /* Init OpenGL via SDL */
  {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_CreateWindowAndRenderer("vt", 800, 600, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL, &window, &renderer);

    int major = 3, minor = 3;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
    printf("OpenGL version %d.%d\n", major, minor);

    SDL_GL_CreateContext(window);

    gladLoadGL();

    if (!GLAD_GL_ARB_draw_instanced) {
      perror("GLAD_GL_ARB_draw_instanced");
      exit(1);
    }
    if(!GLAD_GL_ARB_instanced_arrays)  {
      perror("GLAD_GL_ARB_instanced_arrays");
      exit(1);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }

  /* Initialize Shaders */
  { 
    GLuint vert_shader = 0;
    GLuint frag_shader = 0;
    if (!compile_shader_file("font.vert", GL_VERTEX_SHADER, &vert_shader))
      goto quit;

    if (!compile_shader_file("font.frag", GL_FRAGMENT_SHADER, &frag_shader))
      goto quit;

    GLuint program = 0;
    if (!link_program(vert_shader,frag_shader, &program))
      goto quit;

    grid_uniform = glGetUniformLocation(program, "grid");
    atlas_uniform = glGetUniformLocation(program, "atlas");
    rendering_pass = glGetUniformLocation(program, "renderingPass");

    glUseProgram(program);

    glUniform2f(grid_uniform, COLS, ROWS);
    glUniform1i(rendering_pass, 1);
  }

  GLuint vbo = 0;
  GLuint vao = 0;
  {
    glGenVertexArrays(1, &vao); // almost forgot to initialize the vao first
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(cell_buffer),
        cell_buffer,
        GL_DYNAMIC_DRAW);

    for (Cell_Attr attr = 0; attr < ATTR_CELL_COUNT; attr++) {
      Cell_Attr_Def def = cell_attr_array[attr];
      glEnableVertexAttribArray(attr);
      glVertexAttribPointer(
          attr,
          def.elems,
          GL_FLOAT,
          GL_FALSE,
          sizeof(Cell), 
          (void*) def.offset);
      /* We have these attributes per instance */
      glVertexAttribDivisor(attr, 1); 
    }

  }

  UploadAtlasAndPopulateCharTable("fonts/iosevka-mono.ttf");

  vec4f color = (vec4f) {0.0, 1.0, 0.0, 1.0};

  cell_dim.x = char_table[(int) 'W'].size.x;
  cell_dim.y = FONT_SIZE;

  printf("%d %d\n", cell_dim.x, cell_dim.y);

  struct timeval start, end;
  gettimeofday(&start, NULL);

  term_init(&vt);

  SDL_StartTextInput(window);
  while (running) {

    while (SDL_PollEvent(&event)) {
      switch (event.type) {

        case SDL_EVENT_WINDOW_RESIZED:
          redraw = true;
          screen_dim.x = event.display.data1;
          screen_dim.y = event.display.data2;
          glViewport(0, 0, screen_dim.x, screen_dim.y);
          glUniform2f(grid_uniform, COLS, ROWS);
          break;

        case SDL_EVENT_KEY_DOWN:
          redraw = true;
          switch (event.key.key) {
            case SDLK_RETURN:
              term_cmd_write_char(&vt, '\r');
              break;
            case SDLK_BACKSPACE:
              term_cmd_backspace(&vt);
              break;
            case SDLK_LEFT:
              term_cmd_left(&vt);
              break;
            case SDLK_RIGHT:
              term_cmd_right(&vt);
              break;
          }
          break;
        case SDL_EVENT_TEXT_INPUT:
          term_cmd_write_char(&vt, event.text.text[0]);
          break;
        case SDL_EVENT_QUIT:
          running = false;
          break;
      } /* end of switch case */ 
    } /* end of poll event */

    if (frames%60 == 0) {
      cursor_inverted = !cursor_inverted;
      redraw = true;
    }
    
    if (redraw) {
      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);

      UI_RenderTerm(&vt);

      SDL_GL_SwapWindow(window);
      redraw = false;
    }


    frames++;
    SDL_Delay(16); // 60 fps ish
  }

  gettimeofday(&end, NULL);
  double elapsed = (end.tv_sec - start.tv_sec) + 
    (end.tv_usec - start.tv_usec) / 1e6;

  printf("%ld frames / %.2f seconds = %.2f FPS\n", frames, elapsed, frames / elapsed );

  term_destroy(&vt);
  glDeleteVertexArrays(1, &vao);
  glDeleteBuffers(1, &vbo);

quit:
  {
    puts("Quit successfully!");
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
  }

  return 0;
}
