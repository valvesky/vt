#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <tslib.h>
#include <unistd.h>

/* SDL libraries */
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_events.h>
#include <locale.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <wait.h>

#include "config.h"
#include "vt_term.c"

#define COLOR(c) c.r, c.g, c.b, c.a

int cell_w, cell_h;
int screen_x, screen_y;
uint32_t frame = 0;

#define MAX_COLS 256

void
ui_render_line(SDL_Renderer *renderer, TTF_Font *font, term t, const char *str, const int len, int y, int cell_w, int cell_h) {

  DrawState draw_state = t->draw_state;

  /* Render the glyph background */
  SDL_Rect bg_rect = {
    .x = 0,
    .y = y * cell_h,
    .w = cell_w,
    .h = cell_h
  };

  SDL_SetRenderDrawColor(renderer, draw_state.bg.r, draw_state.bg.g, draw_state.bg.b, draw_state.bg.a);
  SDL_RenderFillRect(renderer, &bg_rect);

  char utf_str[MAX_COLS] = {0};
  memcpy(utf_str, str, len);
  utf_str[len] = '\0';

  SDL_Surface *surf = TTF_RenderText_Blended(font, utf_str, draw_state.fg);
  if (surf) {
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect rect = {
      .x = 0,
      .y = y*cell_h,
      .w = surf->w,
      .h = surf->h
    };
    SDL_RenderCopy(renderer, tex, NULL, &rect);
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(tex);
  }

  /* Render grid lines */
  // SDL_SetRenderDrawColor(renderer, draw_state.fg.r, draw_state.fg.g, draw_state.fg.b, draw_state.fg.a);
  // SDL_RenderDrawLine(renderer, x * cell_w, (y+1) * cell_h - 1, (x+1) * cell_w, (y+1) * cell_h - 1);
  // SDL_RenderDrawLine(renderer, (x+1) * cell_w - 1, y * cell_h, (x+1) * cell_w - 1, (y+1) * cell_h);
}

void
ui_render_cursor(SDL_Renderer *renderer, cursor cs) {

  SDL_Rect rect = { cell_w * cs.x, cell_h * cs.y, cell_w, cell_h };
  // AnsiColor color = (frame/20 % 2 == 0) ? Black : White;
  AnsiColor color = White;
  SDL_SetRenderDrawColor(renderer, COLOR(ansi_bg[color]));
  SDL_RenderFillRect(renderer, &rect);
}


void
ui_render_term(SDL_Renderer *renderer, TTF_Font *font, term sc) {

  char *left = (char*) sc->text.array;
  char *right = left+1;
  char *end = left + sc->text.len + 1;

  int y = 0;

  while (right < end) {

    if (*right == '\n' || right-left == sc->size.x || right == end -1) {
      ui_render_line(renderer, font, sc, left, (long)right-(long)left, y++, cell_w, cell_h);
      left = right+1;
      right++;
    } 

    right++;
  }
}

int
main()
{
  setlocale(LC_ALL, "");
  SDL_Init(SDL_INIT_VIDEO);
  TTF_Init();
  SDL_Window *screen = SDL_CreateWindow("vt", 0, 0, 500, 500, SDL_WINDOW_RESIZABLE);
  SDL_Renderer *renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  TTF_Font *font = TTF_OpenFont("./gomono-nerd.ttf", 12);
  SDL_Event current_event;

  if (!font) {
    printf("Font loading failed: %s\n", TTF_GetError());
    goto quit;
  }


  TTF_SizeText(font, "W", &cell_w, &cell_h);
  SDL_GetRendererOutputSize(renderer, &screen_x, &screen_y);

  Term t = term_init((size) {screen_x/cell_w, screen_y/cell_h});

  ui_render_term(renderer, font, &t);


  SDL_StartTextInput();

  bool running = true;
  bool needs_redraw = true;

  while (running) {   

      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
      SDL_RenderClear(renderer);
      ui_render_term(renderer, font, &t);
      ui_render_cursor(renderer, t.cursor);
      SDL_RenderPresent(renderer);
      needs_redraw = false;

    while (SDL_PollEvent(&current_event)) {
      switch (current_event.type) {
        case SDL_QUIT:
          running = false;
          break;

        case SDL_KEYDOWN:
          needs_redraw = true;
          
          term_write_chr(&t, current_event.key.keysym.sym );

          // SDLK_KP_BACKSPACE
          break;

        case SDL_TEXTINPUT:
          needs_redraw = true;
          break;

        case SDL_WINDOWEVENT:
          switch (current_event.window.event) {
            case SDL_WINDOWEVENT_SIZE_CHANGED:
              SDL_GetRendererOutputSize(renderer, &screen_x, &screen_y);
              needs_redraw = true;
              break;

            case SDL_WINDOWEVENT_EXPOSED:
            case SDL_WINDOWEVENT_RESTORED:
              needs_redraw = true;
              break;
          }
          break;
      }
    } 

    frame++;
    SDL_Delay(60);
  }


quit:
  term_destroy(&t);
  TTF_CloseFont(font);
  TTF_Quit();
  if (renderer) SDL_DestroyRenderer(renderer);
  if (screen) SDL_DestroyWindow(screen);
  SDL_StopTextInput();
  SDL_Quit();
  return 0;
}
