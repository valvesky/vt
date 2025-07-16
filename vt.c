#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_render.h>
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

#include "vt_screen.c"
#include "vt_shell.c"

Screen sc_main;
int cell_w, cell_h;
int screen_x, screen_y;

/* === Terminal Interface === */
typedef struct {
  Screen sc;
  char cmd_buf[CMD_BUFSIZE];  // 512 bytes
  Shell sh;                   // 12 bytes (4 byte alignment)
  UTF8Decoder decoder;        // 8 bytes  (4 byte alignment)
  Glyth draw_state;
  uint16_t cursor_x;      // 2 bytes
  uint16_t cursor_y;      // 2 bytes
  uint16_t nlines;
} Term;


void ui_render_glyth(SDL_Renderer *renderer, TTF_Font *font, const Glyth *glyth, 
                    int x, int y, int cell_w, int cell_h) {
    /* Render the glyph background */
    SDL_Rect bg_rect = {
        .x = x * cell_w,
        .y = y * cell_h,
        .w = cell_w,
        .h = cell_h
    };
    SDL_SetRenderDrawColor(renderer, glyth->bg.r, glyth->bg.g, glyth->bg.b, glyth->bg.a);
    SDL_RenderFillRect(renderer, &bg_rect);

    /* Render the character */
    if (glyth->codepoint != 0) {  // Only render non-empty cells
        char utf_str[5] = {0};
        memcpy(utf_str, &glyth->codepoint, 4);

        SDL_Surface *surf = TTF_RenderUTF8_Blended(font, utf_str, glyth->fg);
        if (surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_Rect rect = {
                .x = x * cell_w + (cell_w - surf->w)/2,  // Center the glyph
                .y = y * cell_h + (cell_h - surf->h)/2,
                .w = surf->w,
                .h = surf->h
            };
            SDL_RenderCopy(renderer, tex, NULL, &rect);
            SDL_FreeSurface(surf);
            SDL_DestroyTexture(tex);
        }
    }

    /* Render grid lines */
    SDL_SetRenderDrawColor(renderer, glyth->fg.r, glyth->fg.g, glyth->fg.b, glyth->fg.a);
    
    // Horizontal line at bottom of cell
    SDL_RenderDrawLine(renderer, 
        x * cell_w, (y+1) * cell_h - 1, 
        (x+1) * cell_w, (y+1) * cell_h - 1);
    
    // Vertical line at right of cell
    SDL_RenderDrawLine(renderer, 
        (x+1) * cell_w - 1, y * cell_h,
        (x+1) * cell_w - 1, (y+1) * cell_h);
}

void
ui_render_screen(SDL_Renderer *renderer, TTF_Font *font, screen sc) {
  // printf("Rendering screen of size %d x %d\n", sc->size.x , sc->size.y);

  for (uint16_t p = 0; p < sc->size.y*sc->size.x; p++) {
    ui_render_glyth(renderer, font, sc->matrix+p, p%sc->size.x, p/sc->size.x, cell_w, cell_h);
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

  sc_main = screen_init((size) {screen_x/cell_w, screen_y/cell_h});

  screen_write_chr(&sc_main, 'h');
  screen_write_chr(&sc_main, 'e');
  screen_write_chr(&sc_main, 'l');
  screen_write_chr(&sc_main, 'l');
  screen_write_chr(&sc_main, 'o');
  screen_write_chr(&sc_main, '\n');
  screen_write_chr(&sc_main, 'h');
  screen_write_chr(&sc_main, 'e');
  screen_write_chr(&sc_main, 'l');
  screen_write_chr(&sc_main, 'l');
  screen_write_chr(&sc_main, 'o');
  // screen_write_chr(&sc_main, '!');


  SDL_StartTextInput();

  bool running = true;
  bool needs_redraw = true;


  while (running) {

    /* Only redraw screen when needed */
    if (needs_redraw) {
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
      SDL_RenderClear(renderer);
      screen_resize(&sc_main, (size) {screen_x/cell_w, screen_y/cell_h});
      ui_render_screen(renderer, font, &sc_main);
      SDL_RenderPresent(renderer);
      needs_redraw = false;
    }

    /* Wait for events */
    SDL_WaitEvent(&current_event);
    do {
      switch (current_event.type) {
        case SDL_QUIT:
          running = false;
          break;

        case SDL_KEYDOWN:
          needs_redraw = true;
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
    } while (SDL_PollEvent(&current_event));
  }


quit:
  screen_destroy(&sc_main);
  TTF_CloseFont(font);
  TTF_Quit();
  if (renderer) SDL_DestroyRenderer(renderer);
  if (screen) SDL_DestroyWindow(screen);
  SDL_StopTextInput();
  SDL_Quit();
  return 0;
}
