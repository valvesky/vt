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
#include <time.h>
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
  wchar_t unicode;
  vec4f foreground;
  vec4f background;
} Cell;

Character char_table[128]; // contains glyth information for each character
Cell cell_buffer[GLYTH_BUF_SIZ]; // instanced cells to be rendered
size_t cell_buffer_pos = 0;

GLuint font_atlas = 0; 

int ascent = 0;
int descent = 0;
int line_gap = 0;
float scale = 0;

Term vt;

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

  glGenTextures(1, &font_atlas);
  glBindTexture(GL_TEXTURE_2D, font_atlas);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, ATLAS_WIDTH, ATLAS_HEIGHT, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

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

  // glBindTexture(GL_TEXTURE_2D, ch.textureid);
  // glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
  // glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

}

void
ui_render_text(char *text, size_t len, vec2f pos, vec4f color) {

  for (size_t i = 0; i < len; i++) {
    Character ch = char_table[(int)text[i]];

    float w = (float) ch.size.x / cell_dim.x;
    float h = (float) ch.size.y / cell_dim.y;

    float offset_y = (float) ch.bearing.y/FONT_SIZE;
    printf("bearing = %d cell_height = %f descent = %.2f\n", ch.bearing.y, FONT_SIZE, offset_y);
    float y = (ROWS-pos.y-2) - offset_y;
    float x = pos.x + (1-w)/2;

    GLfloat vertices[4][4] = {
      { x,   y+h, ch.uv_min.x, ch.uv_min.y }, // Top-left
      { x+w, y+h, ch.uv_max.x, ch.uv_min.y }, // Top-right
      { x,   y,   ch.uv_min.x, ch.uv_max.y }, // Bottom-left
      { x+w, y,   ch.uv_max.x, ch.uv_max.y }  // Bottom-right
    };

    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    pos.x += 1;
    if (pos.x+1 > (int) COLS) {
      pos.y++;
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
  GLuint grid_uniform;
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


    glUniform2f(grid_uniform, COLS, ROWS);
    glUseProgram(program);
  }

  {
    GLuint vbo = 0;
    GLuint vao = 0;
    GLuint ebo = 0;
    glGenVertexArrays(1, &vao); // almost forgot to initialize the vao first
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat)*4*4, NULL, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);

    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

    GLuint indices[] = {
      0, 1, 2,
      2, 1, 3
    };

    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
  }

  UploadAtlasAndPopulateCharTable("fonts/iosevka-mono.ttf");

  vec4f color = (vec4f) {1.0, 1.0, 1.0, 1.0};

  cell_dim.x = char_table[(int) 'W'].size.x;
  cell_dim.y = FONT_SIZE;

  printf("%d %d\n", cell_dim.x, cell_dim.y);

  bool running = true;
  bool redraw = true;

  double elapsed = 0.0;
  uint64_t frames = 0;
  clock_t start = clock();

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

    if (redraw) {
      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);

      LogicalLine cmd = term_ll_get_nonterminated(&vt);

      ui_render_text(vt.scrollback.buffer+cmd.start, cmd.len+vt.cmd_len, (vec2f) {0, 0}, color);
      // ui_render_cursor();

      SDL_GL_SwapWindow(window);
      redraw = false;
    }

    frames++;
    SDL_Delay(60);
  }

  clock_t end = clock();
  elapsed = ((float) (end-start)/CLOCKS_PER_SEC);
  printf("%ld frames / %f seconds = %f FPS\n", frames, elapsed, frames / elapsed );

  term_destroy(&vt);
  // glDeleteVertexArrays(1, &vao);
  // glDeleteBuffers(1, &vbo);

quit:
  {
    puts("Quit successfully!");
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
  }

  return 0;
}
