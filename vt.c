#define _GNU_SOURCE // has to be at the top
/*
 *                  _/
 *   _/      _/  _/_/_/_/
 *  _/      _/    _/
 *   _/  _/      _/
 *    _/          _/_/
 *
*/

#include "lib/glad.c"
#define STB_TRUETYPE_IMPLEMENTATION
#include "lib/stb_truetype.h"

/* SDL3 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

/* headers for all *.c files (this is a unity build) */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <pty.h>

#include "config.h"
#include "vt_circ_buf.c"
#include "vt_vec.c"
#include "vt_opengl.c"
#include "vt_term.c"


SDL_Window *sdl_window;
SDL_Renderer *sdl_renderer;
SDL_Event sdl_event;

static uint64_t frames = 0;
static bool running = true;
static bool redraw = true;
static bool cursor_inverted = true;

static Renderer renderer;
static Terminal vt;

int main() {

  /* Init OpenGL via SDL */
  {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_CreateWindowAndRenderer("vt", 800, 600, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL, &sdl_window, &sdl_renderer);

    int major = 3, minor = 3;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
    printf("OpenGL version %d.%d\n", major, minor);

    SDL_GL_CreateContext(sdl_window);

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

  {
    int w, h;
    SDL_GetRenderOutputSize(sdl_renderer, &w, &h);
    renderer = Renderer_Create("fonts/iosevka-mono.ttf", 42, w, h);
    vt       = Terminal_Create(&renderer);
  }

  struct timeval start, end;
  gettimeofday(&start, NULL);

  SDL_StartTextInput(sdl_window);
  while (running) {

    while (SDL_PollEvent(&sdl_event)) {
      switch (sdl_event.type) {

        case SDL_EVENT_WINDOW_RESIZED:
          redraw = true;
          Renderer_ResizeScreen(&renderer, sdl_event.display.data1, sdl_event.display.data2);
          break;

        case SDL_EVENT_KEY_DOWN:
          redraw = true;
          switch (sdl_event.key.key) {
            case SDLK_RETURN: 
              Terminal_CMD_Write(&vt, "\r", 1);
              break;
            case SDLK_BACKSPACE:
              Terminal_CMD_Backspace(&vt);
              break;
            case SDLK_LEFT:
              Terminal_CMD_Left(&vt);
              break;
            case SDLK_RIGHT:
              Terminal_CMD_Right(&vt);
              break;
          }
          break;
        case SDL_EVENT_TEXT_INPUT:
          Terminal_CMD_Write(&vt, sdl_event.text.text, 1);
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
      term_render(&vt, 0);
      Renderer_Draw(&renderer);
      SDL_GL_SwapWindow(sdl_window);
      redraw = false;
    }

    frames++;

    if (frames % 60 == 0) {
      gettimeofday(&end, NULL);
      double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
      char buf[128];
      sprintf(buf, "vt - %d x %d %.2f FPS", renderer_get_rowsi(&renderer), renderer_get_colsi(&renderer), (float) frames / elapsed );
      SDL_SetWindowTitle(sdl_window, buf);
    }

    SDL_Delay(8); // 120 fps ish
  }

  Terminal_Destroy(&vt);
  Renderer_Destroy(&renderer);
  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(sdl_window);
  SDL_Quit();
  puts("Quit successfully!");
  return 0;
}
