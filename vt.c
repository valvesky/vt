#include "vt_circ_buf.c"

#include "lib/glad.c"

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

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Event event;

character_t char_table[128];

GLint color_uniform;

character_t GetCharacter(int c) {
  if (c >= 128) c = '?';
  return char_table[c]; 
}

Term vt;



void
ui_render_text(char *text, size_t len, vec2f pos, vec4f color) {

  glUniform4f(color_uniform, color.i, color.j, color.k, color.l);

  for (size_t i = 0; i < len; i++) {
    character_t ch = char_table[(int)text[i]];

    float w = (float) ch.size.x / cell_dim.x;
    float h = (float) ch.size.y / cell_dim.y;

    float descent = (float) (ch.size.y - ch.bearing.y) / cell_dim.y;
    float y = (ROWS - pos.y - 1) - descent;
    float x = pos.x + (1-w)/2;

    GLfloat vertices[4][4] = {
      { x,     y+h,    0.0f, 0.0f }, // Top-left
      { x+w,   y+h,    1.0f, 0.0f }, // Top-right
      { x,     y,      0.0f, 1.0f }, // Bottom-left
      { x+w,   y,      1.0f, 1.0f }  // Bottom-right
    };

    glBindTexture(GL_TEXTURE_2D, ch.textureid);
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
  GLint grid_uniform;
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


    color_uniform = glGetUniformLocation(program, "textColor");
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

  {
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
      perror("Freetype2");
      goto quit;
    }

    FT_Face face;
    if (FT_New_Face(ft, "fonts/iosevka-mono.ttf", 0, &face)) {
      perror("Freetype2");
      goto quit;
    }

    FT_Set_Pixel_Sizes(face, 0, FONT_SIZE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    for (int c = 0; c < 128; c++) {
      if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
        perror("Faied to load glyth");
        continue;
      }

      unsigned int texture;
      glGenTextures(1, &texture);
      glBindTexture(GL_TEXTURE_2D, texture);
      glTexImage2D(
          GL_TEXTURE_2D,
          0,
          GL_RED,
          face->glyph->bitmap.width,
          face->glyph->bitmap.rows,
          0,
          GL_RED,
          GL_UNSIGNED_BYTE,
          face->glyph->bitmap.buffer
          );

      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      char_table[c] = (character_t) {
        texture, 
        (vec2i) {face->glyph->bitmap.width, face->glyph->bitmap.rows},
        (vec2i) {face->glyph->bitmap_left, face->glyph->bitmap_top},
        face->glyph->advance.x
      };
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);
  }

  vec4f color = (vec4f) {1.0, 1.0, 1.0, 1.0};

  cell_dim.x  = char_table[(int) 'W'].size.x;
  cell_dim.y = char_table[(int) 'W'].size.y  + char_table[(int) 'g'].bearing.y;

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
          glUniformMatrix4fv(projection_uniform, 1, GL_FALSE, &projection.col0.i);
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
