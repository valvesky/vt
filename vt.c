#define _GNU_SOURCE // has to be at the top                                                                                                                                             
/*
 *                  _/
 *   _/      _/  _/_/_/_/
 *  _/      _/    _/
 *   _/  _/      _/
 *    _/          _/_/
 *
*/

// #pragma GCC poison malloc
// #pragma GCC poison free

#define malloc  SDL_malloc
#define free    SDL_free
#define calloc  SDL_calloc
#define realloc SDL_realloc

#define STB_TRUETYPE_IMPLEMENTATION
#include "lib/stb_truetype.h"

/* SDL3 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_video.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef _VT_OPENGL 
#include "opengl/glad-3.3.c"
#include <SDL3/SDL_opengl.h>
#include "vt_renderer_opengl.c"
#else
#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>
#include "vt_renderer_vulkan.c"
#endif

static Renderer renderer;

int main() {
  
  if (!renderer_create(&renderer)) {
    return 1;
  }

  bool running = true;
  SDL_Event event;
  while (running) {
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_EVENT_QUIT:
          running = false;
      }
    }
  }

  renderer_destroy(&renderer);
  puts("Quit successfully!");
  return 0;
}

