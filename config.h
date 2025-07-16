#ifndef _CONFIG_H_
#define _CONFIG_H_
#include <SDL2/SDL.h>

static const float alpha = 0.7;

/* Colors */
static const SDL_Color ansi_fg[] = {
  {0, 0, 0, 255},       // 30: Black
  {220, 50, 50, 255},   // 31: Red
  {50, 185, 50, 255},   // 32: Green
  {200, 185, 50, 255},  // 33: Yellow
  {50, 50, 220, 255},   // 34: Blue
  {200, 50, 200, 255},  // 35: Magenta
  {50, 185, 185, 255},  // 36: Cyan
  {255, 255, 255, 255}  // 37: White
};


static const SDL_Color ansi_bg[] = {
  {0, 0, 0,       200},       // 40: Black
  {128, 0, 0,     200},     // 41: Red
  {0, 128, 0,     200},     // 42: Green
  {128, 128, 0,   200},   // 43: Yellow
  {0, 0, 128,     200},     // 44: Blue
  {128, 0, 128,   200},   // 45: Magenta
  {0, 128, 128,   200},   // 46: Cyan
  {200, 200, 200, 200}  // 47: White
};

#endif
