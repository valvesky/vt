#include "vt_circ_buf.c"

#include "lib/glad.c"
#include "vt_opengl.h"

/* maybe switch to stb in the future */
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
#include <stddef.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "vt_vec.c"
#include "vt_opengl.c"
#include "vt_term.c"


SDL_Window *window;
SDL_Renderer *renderer;
SDL_Event event;

static uint64_t frames = 0;
static bool running = true;
static bool redraw = true;
static bool cursor_inverted = true;

static void UI_RenderLogicalLine(term_t term, LogicalLine *ll, vec2f pos);
static void UI_RenderTerm(term_t vt);

static Terminal vt;


static void 
UI_RenderLogicalLine_ANSI(term_t term, LogicalLine *ll, vec2f pos) {

  char *text = term_scrollback_get(term, ll->start);
  size_t len = ll->len;
  vec4f color = term->cursor_real.fg;
  bool in_esc = false;

  for (const char *const end = text+len; text < end; text++) {

    if (*text == ESC) {
      in_esc = true;
      continue;
    }

    if (!(32 <= *text && *text <= 127)) {
      continue; /* skip glyths we cannot render */
    }

    Character ch = GetCharacter((int)text[i]);

    /*  Glyth position    Atlas uv coords
     *    |   
     *  h |              +-------uv_max
     *    |              |           |
     *   x,y------       uv_min -----+
     *        w
     */

    float desc     = (descent * scale) / cell_dim.y;
    float bearingY = ((float)ch.bearing.y) / cell_dim.y;
    float h        = ((float)ch.size.y) / cell_dim.y;
    float w        = (float)ch.size.x / cell_dim.x;

    float x = pos.x + ((1-w)/2);
    float y = (pos.y) - (bearingY + h - desc);

    Cell new = {
      .pos = { x, y, w, h},
      .uv  = { ch.uv_max.x, ch.uv_max.y, ch.uv_min.x, ch.uv_min.y},
      .fg  = color,
      .bg  = { 0.0, 0.0, 0.0, 1.0},
    };

    CellBufferPush(new);
    pos.x += 1;

    if (pos.x+1 > (int) COLS) {
      pos.y--;
      pos.x = 0;
    }
  }
}

static void 
UI_RenderLogicalLine(term_t term, LogicalLine *ll, vec2f pos) {

  char *text = term_scrollback_get(term, ll->start);
  size_t len = ll->len;
  vec4f color = term->draw_state.fg;

  for (size_t i = 0; i < len; i++) {
    assert(text[i] != ESC);
    if (!(32 <= text[i] && text[i] <= 127)) {
      continue; /* skip glyths we cannot render */
    }

    Character ch = GetCharacter((int)text[i]);

    /*  Glyth position    Atlas uv coords
     *    |   
     *  h |              +-------uv_max
     *    |              |           |
     *   x,y------       uv_min -----+
     *        w
     */

    float desc     = (descent * scale) / cell_dim.y;
    float bearingY = ((float)ch.bearing.y) / cell_dim.y;
    float h        = ((float)ch.size.y) / cell_dim.y;
    float w        = (float)ch.size.x / cell_dim.x;

    float x = pos.x + ((1-w)/2);
    float y = (pos.y) - (bearingY + h - desc);

    Cell new = {
      .pos = { x, y, w, h},
      .uv  = { ch.uv_max.x, ch.uv_max.y, ch.uv_min.x, ch.uv_min.y},
      .fg  = color,
      .bg  = { 0.0, 0.0, 0.0, 1.0},
    };

    CellBufferPush(new);
    pos.x += 1;

    if (pos.x+1 > (int) COLS) {
      pos.y--;
      pos.x = 0;
    }
  }
}

static void
UI_RenderTerm(term_t term) {

  /* Get last logical line that fits on the screen */
  size_t virtual_line = 0;
  int idx = 0;
  LogicalLine ll;

  while (idx <= LL_COUNT(term) && virtual_line < (size_t) ROWS) {
    ll = term_ll_get_last(term, idx);
    virtual_line += term_ll_get_visual_lines(ll, COLS);
    if (idx == LL_COUNT(term)) break;
    idx++;
  }

  /* render from the top */
  if (idx == LL_COUNT(term) && virtual_line < ROWS-1) {
    float floor = (float) ((int) ROWS);
    vec2f pos = {0, floor-1};
    for (int i = idx; i >= 0; i--) {
      ll = term_ll_get_last(term, i);
      UI_RenderLogicalLine(term, &ll, pos); 
      pos.y -= term_ll_get_visual_lines(ll, COLS);
    }
  } else {
    /* render from the bottom */
    vec2f pos = {0, 0};
    for (int i = 0; i <= idx; i++) {
      ll = term_ll_get_last(term, i);
      pos.y += term_ll_get_visual_lines(ll, COLS);
      UI_RenderLogicalLine(term, &ll, pos); 
    }
  }

  CellBufferRender();
  cell_buffer_pos = 0;
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

  Renderer renderer = Renderer_Create();

  struct timeval start, end;
  gettimeofday(&start, NULL);

  term_init(&vt);

  SDL_StartTextInput(window);
  while (running) {

    while (SDL_PollEvent(&event)) {
      switch (event.type) {

        case SDL_EVENT_WINDOW_RESIZED:
          redraw = true;
          Renderer_Resize(&renderer, event.display.data1, event.display.data2);
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

    if (frames % 60 == 0) {
      gettimeofday(&end, NULL);
      double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
      char buf[128];
      sprintf(buf, "vt - %.1fx%.1f %.2f FPS\0", ROWS, COLS, frames / elapsed );
      SDL_SetWindowTitle(window, buf);
    }

    SDL_Delay(8); // 60 fps ish
  }

  term_destroy(&vt);

  Renderer_Destroy(&renderer);

quit:
  {
    puts("Quit successfully!");
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
  }

  return 0;
}
