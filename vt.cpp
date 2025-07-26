#include "vt_circ_buf.c"

#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_pixels.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <cstdlib>
#include <ctime>
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

#include "vt_term.c"
#include "config.h"

#include <map>

#define COLOR(c) c.r, c.g, c.b, c.a
#define COLS (screen_x/cell_w)
#define ROWS (screen_y/cell_h)

struct Glyth {
  DrawState state;
  union {
    wchar_t unicode;
    char ch;
  };
};

struct Rune {
  SDL_Color fg;
  union {
    wchar_t unicode;
    char ch;
  };
};

int cell_w, cell_h;
int screen_x, screen_y;
uint32_t frame = 0;
static std::map<char, SDL_Texture*> char_texture_map;
static std::map<wchar_t, SDL_Surface*> unicode_surface_map;

uint16_t
ui_render_logical_line(SDL_Renderer *renderer, TTF_Font *font, term_t t, LogicalLines ll, int y, int cell_w, int cell_h) {

  DrawState draw_state = t->draw_state;

  int remainder = ll.len % COLS;
  uint16_t visual_line_num = ll.len/COLS + (remainder>0);

  char *ptr = term_ll_str(t, &ll);
  char *end = ptr + ll.len;

  SDL_Texture *tex;

  static std::map<char, int> char_width; 

  int w = 0;
  int x = 0;
  // if (!ll.has_unicode) {
  while (ptr < end) {
    if (auto search = char_texture_map.find(*ptr); search != char_texture_map.end()) {
      tex = search->second;
    }
    else {
      char temp[2];
      temp[0] = *ptr;
      temp[1] = '\0';
      SDL_Surface *surf = TTF_RenderText_Blended(font, temp, draw_state.fg);
      char_width.insert({*ptr, surf->w});
      tex = SDL_CreateTextureFromSurface(renderer, surf);
      SDL_FreeSurface(surf);
      char_texture_map.insert({*ptr, tex});
    }

    SDL_Rect placement = {x*cell_w + (cell_w-char_width[*ptr])/2, y*cell_h, char_width[*ptr], cell_h};
    x++;
    if (x > COLS) {
      y++;
      x = 0;
    }

    SDL_RenderCopy(renderer, tex, NULL, &placement);
    ptr++;
  }
// }


  /* Render grid lines */
#ifdef DEBUG_GRID
  SDL_SetRenderDrawColor(renderer, draw_state.fg.r, draw_state.fg.g, draw_state.fg.b, draw_state.fg.a);
  SDL_RenderDrawLine(renderer, x * cell_w, (y+1) * cell_h - 1, (x+1) * cell_w, (y+1) * cell_h - 1);
  SDL_RenderDrawLine(renderer, (x+1) * cell_w - 1, y * cell_h, (x+1) * cell_w - 1, (y+1) * cell_h);
#endif

  return visual_line_num;
}

void
ui_render_cursor(SDL_Renderer *renderer, cursor cs) {

  SDL_Rect rect = { cell_w * cs.x, cell_h * cs.y, cell_w, cell_h };
  AnsiColor color = (frame/20 % 2 == 0) ? Black : White;
  // AnsiColor color = White;
  SDL_SetRenderDrawColor(renderer, COLOR(ansi_bg[color]));
  SDL_RenderFillRect(renderer, &rect);
}


void
ui_render_term(SDL_Renderer *renderer, TTF_Font *font, term_t term) {

  for (int i = 0; i <= LL_COUNT(term); i++) {
    ui_render_logical_line(renderer, font, term, term_ll_get_last(term, i), i , cell_w, cell_h);
  }

  // LogicalLines ll = term_ll_get_last(term, 0);
  // ui_render_line(renderer, font, term, term_ll_str(term, &ll), ll.len, 0 , cell_w, cell_h);

}


Term term;

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
    TTF_CloseFont(font);
    TTF_Quit();
    if (renderer) SDL_DestroyRenderer(renderer);
    if (screen) SDL_DestroyWindow(screen);
    SDL_Quit();
    return EXIT_FAILURE;
  }


  TTF_SizeText(font, "W", &cell_w, &cell_h);
  SDL_GetRendererOutputSize(renderer, &screen_x, &screen_y);

  term_init(&term, COLS, ROWS);

  ui_render_term(renderer, font, &term);


  SDL_StartTextInput();

  bool running = true;

  while (running) {   
    clock_t start = clock();


    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    ui_render_term(renderer, font, &term);
    ui_render_cursor(renderer, term.cursor);
    SDL_RenderPresent(renderer);


    while (SDL_PollEvent(&current_event)) {
      switch (current_event.type) {
        case SDL_QUIT:
          running = false;
          break;

        case SDL_KEYDOWN:
          
          term_cmd_write(&term, current_event.key.keysym.sym );

          // SDLK_KP_BACKSPACE
          break;

        case SDL_TEXTINPUT:
          break;

        case SDL_WINDOWEVENT:
          switch (current_event.window.event) {
            case SDL_WINDOWEVENT_SIZE_CHANGED:
              SDL_GetRendererOutputSize(renderer, &screen_x, &screen_y);
              break;

            case SDL_WINDOWEVENT_EXPOSED:
            case SDL_WINDOWEVENT_RESTORED:
              break;
          }
          break;
      }
    } 

    frame++;
    clock_t end = clock();
    printf("FPS %f\n", 1 / ((float) (end-start) / CLOCKS_PER_SEC));
    // SDL_Delay(60);
  }

  term_destroy(&term);
  TTF_CloseFont(font);
  TTF_Quit();
  if (renderer) SDL_DestroyRenderer(renderer);
  if (screen) SDL_DestroyWindow(screen);
  SDL_StopTextInput();
  SDL_Quit();
  return 0;
}
