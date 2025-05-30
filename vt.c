#include "vt.h"
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_ttf.h>
#include <string.h>
#include <tslib.h>

static Geometry geometry;
static Term term;

static void parse_sgr_sequence(const char *seq, Glyth *glyth_state);

static void term_init();
static void term_handle_text(SDL_TextInputEvent text);
static void term_handle_key(SDL_KeyboardEvent key);
static void ui_render_glyth(SDL_Renderer *renderer, TTF_Font *font, const Glyth *glyth, int x, int y, int cell_w, int cell_h);
static void ui_render_grid(SDL_Renderer *renderer, TTF_Font *font);

size_t write_glyth(Glyth *glyth_arr, char *buf, size_t buflen, size_t *consumed);
size_t term_sh_read(Term *t);

static void
parse_sgr_sequence(const char *seq, Glyth *glyth_state) {
  char *token = strtok((char *)seq, ";");
  while (token != NULL) {
    int code = atoi(token);

    if (code == 0) {  // Reset
      *glyth_state = (Glyth){
        .fg = {255, 255, 255, 255},
          .bg = {0, 0, 0, 255},
          .bold = false,
          .underline = false
      };
    }
    else if (code == 1) glyth_state->bold = true;
    else if (code == 4) glyth_state->underline = true;
    else if (code == 22) glyth_state->bold = false;
    else if (code == 24) glyth_state->underline = false;
    // Foreground colors
    else if (code >= 30 && code <= 37) {
      glyth_state->fg = ansi_fg[code - 30];
    }
    // Background colors
    else if (code >= 40 && code <= 47) {
      glyth_state->bg = ansi_bg[code - 40];
    }
    token = strtok(NULL, ";");
  }
}


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

static inline void
term_handle_text(SDL_TextInputEvent text) {
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

      term_sh_read(&term);

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
ui_render_glyth(SDL_Renderer *renderer, TTF_Font *font, const Glyth *glyth, int x, int y, int cell_w, int cell_h) {
  /* Renders a glyth */

  char utf_str[5] = {0};
  memcpy(utf_str, &glyth->utf8, 4); 

  SDL_Surface *surf;
  surf = TTF_RenderUTF8_Blended(font, utf_str, glyth->fg);
  if (!surf) return;

  SDL_Texture *tex;

  tex = SDL_CreateTextureFromSurface(renderer, surf);
  SDL_Rect rect = {
    .x = x * cell_w,
    .y = y * cell_h,
    .w = surf->w,
    .h = surf->h
  };

  // Render background
  SDL_Rect bg_rect = {
    .x = x * cell_w,
    .y = y * cell_h,
    .w = cell_w,
    .h = cell_h
  };

  SDL_SetRenderDrawColor(renderer, glyth->bg.r, glyth->bg.g, glyth->bg.b, glyth->bg.a);
  SDL_RenderFillRect(renderer, &bg_rect);

  // Render character
  SDL_RenderCopy(renderer, tex, NULL, &rect);

  // Render underline
  if (glyth->underline) {
    SDL_Rect underline_rect = {
      .x = x * cell_w,
      .y = (y + 1) * cell_h - 2,
      .w = cell_w,
      .h = 1
    };

    SDL_SetRenderDrawColor(renderer, glyth->fg.r, glyth->fg.g, glyth->fg.b, glyth->fg.a);
    SDL_RenderFillRect(renderer, &underline_rect);
  }

  SDL_FreeSurface(surf);
  SDL_DestroyTexture(tex);
}

static void
ui_render_grid(SDL_Renderer *renderer, TTF_Font *font) {

  /* Precompute fixed cell dimensions */
  static int cell_w, cell_h;
  TTF_SizeText(font, "W", &cell_w, &cell_h);

  Glyth *buf_ptr = term.buffer;
  Glyth *buf_end = term.buffer + BUFSIZ - 1;

  int x = 0;
  int y = 0;

  while (buf_ptr < buf_end && buf_ptr != NULL) {

    if (buf_ptr->utf8 == L'\n') {
      x = 0;
      y += 1;
    } else if (buf_ptr->utf8 == L'\t') {
      x += 4;
      if ( x*cell_w >= geometry.screen_x) {
        x = 0;
        y += 1;
      }
    }

    ui_render_glyth(renderer, font, buf_ptr, x, y, cell_w, cell_h);
    x++;

    if ( x*cell_w >= geometry.screen_x) {
      x = 0;
      y += 1;
    }
    buf_ptr++;
  }

}

size_t
write_glyth(Glyth *glyth_arr, char *buf, size_t buflen, size_t *consumed) {
  Glyth *glyth_ptr = glyth_arr;
  Glyth glyth_state = {
    .fg = ansi_fg[7],
    .bg = ansi_bg[0],
    .bold = false,
    .underline = false
  };

  size_t glyths_written = 0;
  *consumed = 0;

  char *buf_ptr = buf;
  char *buf_end = buf + buflen;

  bool in_escape = false;
  bool saw_bracket = false;
  char escape_seq[32];
  int escape_idx = 0;

  while (buf_ptr < buf_end) {
    if (!in_escape) {
      if (*buf_ptr == '\x1B') {  // ESC
        in_escape = true;
        saw_bracket = false;
        escape_idx = 0;
        buf_ptr++;
        (*consumed)++;
        continue;
      }

      // Normal printable character
      glyth_state.utf8 = (wchar_t)*buf_ptr;
      *glyth_ptr++ = glyth_state;
      glyths_written++;

      buf_ptr++;
      (*consumed)++;
      continue;
    }

    // We're inside an escape sequence
    if (!saw_bracket) {
      if (*buf_ptr == '[') {
        saw_bracket = true;
        buf_ptr++;
        (*consumed)++;
        continue;
      } else {
        // Not a CSI, abort escape
        in_escape = false;
        continue;
      }
    }

    // Reading CSI arguments until we hit 'm'
    if ((*buf_ptr >= '0' && *buf_ptr <= '9') || *buf_ptr == ';') {
      if (escape_idx < (int)sizeof(escape_seq) - 1) {
        escape_seq[escape_idx++] = *buf_ptr;
      }
      buf_ptr++;
      (*consumed)++;
      continue;
    }

    if (*buf_ptr == 'm') {
      // End of SGR sequence
      escape_seq[escape_idx] = '\0';
      parse_sgr_sequence(escape_seq, &glyth_state);

      in_escape = false;
      escape_idx = 0;
      saw_bracket = false;

      buf_ptr++;
      (*consumed)++;
      continue;
    }

    // Unexpected character inside CSI
    in_escape = false;
    escape_idx = 0;
    saw_bracket = false;
  }

  return glyths_written;
}

size_t
term_sh_read(Term *t) {
  /* Reads raw output from the shell to a buffer 
   * So it can be parsed into glyths */

  static char buf[4096];
  static size_t buflen = 0;

  ssize_t r = read(t->sh.fd, buf + buflen, sizeof(buf) - buflen - 1);
  if (r < 0) {
    perror("read");
    return 0;
  } else if (r == 0) {
    t->sh.active = false;
    return 0;
  }

  buflen += r;

  size_t consumed = 0;
  size_t n_glyths = write_glyth(t->buffer, buf, buflen, &consumed);

  /* Move buffer of chars to consume backwards */
  if (consumed > 0 && consumed < buflen) {
    memmove(buf, buf+consumed, buflen - consumed);
  }

  term.buf_pos += (uint16_t) n_glyths;
  return r;
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

  bool running = true;
  bool needs_redraw = true;
  int frame = 0;

  SDL_GetRendererOutputSize(renderer, &geometry.screen_x, &geometry.screen_y);
  while (running) {
    printf("Frame: %d\n", frame++);

    /* Only redraw screen when needed */
    if (needs_redraw) {
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
      SDL_RenderClear(renderer);
      ui_render_grid(renderer, font);
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
          term_handle_key(current_event.key);
          needs_redraw = true;
          break;

        case SDL_TEXTINPUT:
          term_handle_text(current_event.text);
          needs_redraw = true;
          break;

        case SDL_WINDOWEVENT:
          switch (current_event.window.event) {
            case SDL_WINDOWEVENT_SIZE_CHANGED:
              SDL_GetRendererOutputSize(renderer, &geometry.screen_x, &geometry.screen_y);
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
  term_destroy();

  TTF_CloseFont(font);
  TTF_Quit();
  if (renderer) SDL_DestroyRenderer(renderer);
  if (screen) SDL_DestroyWindow(screen);
  SDL_StopTextInput();
  SDL_Quit();
  return 0;
}
