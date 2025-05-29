#include "vt.h"
#include <SDL2/SDL_keycode.h>
#include <string.h>
#include <tslib.h>

#define CMD_BUFSIZE 512

typedef struct {
  int screen_x;
  int screen_y;
} Geometry;

typedef struct {
  char buffer[BUFSIZ];
  char cmd_buf[CMD_BUFSIZE];
  uint16_t cmd_buf_pos;
  uint16_t cmd_cursor_pos;
  Shell sh;
} Term;

static Geometry geometry;
static Term term;

static bool running = true;

static void term_init();
static void term_handle_text(SDL_TextInputEvent text);
static void term_handle_key(SDL_KeyboardEvent key);

static void
term_init() {
  memset(&term, 0, sizeof(Term));
  term.sh = shell_init();
}

static void
term_destroy() {
  puts("Waiting for terminal to destroy");
  shell_destroy(&term.sh);
  memset(&term, 0, sizeof(Term));
}

static inline
void term_handle_text(SDL_TextInputEvent text) {
  // Append text to buffer
  char* input = text.text;
  while (*input && term.cmd_cursor_pos < CMD_BUFSIZE - 1) {
    term.cmd_buf[term.cmd_cursor_pos++] = *input++;
    term.cmd_buf[term.cmd_cursor_pos] = '\0';  // Keep null-terminated
  }
}

static inline void
term_handle_key(SDL_KeyboardEvent key) {

  switch(key.keysym.sym) {
    case SDLK_RETURN:
      // fallthrough
    case SDLK_KP_ENTER:
      // Add newline (or carriage return) to the command
      term.cmd_buf[term.cmd_cursor_pos++] = '\n';
      term.cmd_buf[term.cmd_cursor_pos] = '\0';

      ssize_t written = write(term.sh.fd, term.cmd_buf, term.cmd_cursor_pos);
      if (written < 0)
        perror("write");

      char buffer[4096];
      ssize_t n = read(term.sh.fd, buffer, sizeof(buffer));
      if (n > 0) {
        strcat(term.buffer, buffer);
      }

      // Reset buffer
      memset(term.cmd_buf, 0, sizeof(term.cmd_buf));
      term.cmd_cursor_pos = 0;
      return;

    case SDLK_c:  // Handle Ctrl+C
      if(SDL_GetModState() & KMOD_CTRL) {
        printf("\n^C\n");
        term.cmd_cursor_pos = 0;
        memset(term.cmd_buf, 0, CMD_BUFSIZE);
      }
      return;
    default:
      return;
  }
}

static void
ui_render_grid(SDL_Renderer *renderer, TTF_Font *font) {

  /* Precompute fixed cell dimensions */
  int cell_w, cell_h;
  TTF_SizeText(font, "W", &cell_w, &cell_h);
  if (cell_w == 0 || cell_h == 0) {
    cell_w = 10;
    cell_h = 20;
  }

  SDL_Surface *surf;
  SDL_Texture *tex;
  char ch[2] = {0, 0};  // Ensure null-terminated string

  char *buf_ptr;
  char *buf_end = term.buffer + BUFSIZ - 1;

  int x = 0;
  int y = 0;
  for (buf_ptr = term.buffer; buf_ptr != buf_end; buf_ptr++) {
    if (*buf_ptr == 0) break;
    if (*buf_ptr == '\n') {
      x = 0;
      y += 1;
      continue;
    }

    ch[0] = *buf_ptr;
    surf = TTF_RenderText_Solid(font, ch, (SDL_Color){255, 255, 255, 255});
    if (!surf) continue;  // rendering fails

    tex = SDL_CreateTextureFromSurface(renderer, surf);

    /* Render character centered in fixed cell */
    SDL_Rect rect = {
      x * cell_w + (cell_w - surf->w) / 2,  // Center horizontally
      y * cell_h + (cell_h - surf->h) / 2,  // Center vertically
      surf->w,
      surf->h
    };

    SDL_RenderCopy(renderer, tex, NULL, &rect);
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(tex);
    x++;
  }

  /* Render Command Line After everything */
  x = 0;
  y += 1;
  buf_end = term.cmd_buf + CMD_BUFSIZE - 1;
  for (buf_ptr = term.cmd_buf; buf_ptr != buf_end; buf_ptr++) {
    if (*buf_ptr == 0) break;
    if (*buf_ptr == '\n') {
      x = 0;
      y += 1;
      continue;
    }

    ch[0] = *buf_ptr;
    surf = TTF_RenderText_Solid(font, ch, (SDL_Color){255, 255, 255, 255});
    if (!surf) continue;  // rendering fails

    tex = SDL_CreateTextureFromSurface(renderer, surf);

    SDL_Rect rect = {
      x * cell_w ,
      y * cell_h + (cell_h - surf->h) / 2,
      surf->w,
      surf->h
    };

    if (rect.x+cell_w >= geometry.screen_x) {
      x = 0;
      rect.x = 0;
      rect.y = ++y * cell_h + (cell_h - surf->h) / 2;
    }

    SDL_RenderCopy(renderer, tex, NULL, &rect);
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(tex);
    x++;
  }
}

int
main(void) {
  /* Shell is a fork, must be the first thing to init */
  setlocale(LC_ALL, "");

  SDL_Init(SDL_INIT_VIDEO);
  TTF_Init();
  SDL_Window *screen = SDL_CreateWindow("vt", 0, 0, 500, 500, SDL_WINDOW_RESIZABLE);
  SDL_Renderer *renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  TTF_Font *font = TTF_OpenFont("./gomono-nerd.ttf", 24);
  SDL_Event current_event;

  if (!font) {
    printf("Font loading failed: %s\n", TTF_GetError());
    goto quit;
  }

  /* setup stdin */
  fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);

  /* init terminal struct */
  term_init();
  SDL_StartTextInput();

  strcpy(term.buffer, "Hello my\nPeople Say\0");

  while (running) {

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_GetRendererOutputSize(renderer, &geometry.screen_x, &geometry.screen_y);

    /* Read from stdin */
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    struct timeval timeout = {0, 0}; // Non-blocking check

    if (select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout) > 0) {
      char buffer[256];
      ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer)-1);

      if (count > 0) {
        buffer[count] = '\0'; // Null-terminate
        printf("Received from stdin: %s", buffer);
      }
    }


    /* Event loop */
    while (SDL_PollEvent(&current_event)) {

      switch (current_event.type) {
        case SDL_QUIT:
          running = false;
          break;
        case SDL_KEYDOWN:
          term_handle_key(current_event.key);
          break;
        case SDL_TEXTINPUT:
          term_handle_text(current_event.text);
          break;
      }

    } /* end of event loop */

    ui_render_grid(renderer, font);
    SDL_RenderPresent(renderer);
    SDL_Delay(10);
  }

quit:
  term_destroy();

  TTF_CloseFont(font);
  TTF_Quit();
  if (renderer) SDL_DestroyRenderer(renderer);
  if (screen) SDL_DestroyWindow(screen);
  SDL_StopTextInput();
  SDL_Quit();
  return 0;
}
