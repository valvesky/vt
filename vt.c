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
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_video.h>

#include "vt_renderer_opengl.c"

static Renderer renderer;

int main() {
  renderer = renderer_create();

  renderer_destroy(&renderer);
  return 0;
}

