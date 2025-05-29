#include "vt.h"
#include <SDL2/SDL_keycode.h>
#include <string.h>
#include <tslib.h>

static Geometry geometry;
static Term term;

static bool running = true;

static void term_init();
static void term_handle_text(SDL_TextInputEvent text);
static void term_handle_key(SDL_KeyboardEvent key);
// static int term_write(const char *src, int buflen, Term *t);

static void parse_sgr_sequence(const char *seq, GlythState *state) {
    char *token = strtok((char *)seq, ";");
    while (token != NULL) {
        int code = atoi(token);
        
        if (code == 0) {  // Reset
            *state = (GlythState){
                .fg = {255, 255, 255, 255},
                .bg = {0, 0, 0, 255},
                .bold = false,
                .underline = false
            };
        }
        else if (code == 1) state->bold = true;
        else if (code == 4) state->underline = true;
        else if (code == 22) state->bold = false;
        else if (code == 24) state->underline = false;
        // Foreground colors
        else if (code >= 30 && code <= 37) {
            state->fg = ansi_fg[code - 30];
        }
        // Background colors
        else if (code >= 40 && code <= 47) {
            state->bg = ansi_bg[code - 40];
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
ui_render_char(SDL_Renderer *renderer, TTF_Font *font, char ch, 
                       const GlythState *state, int x, int y, 
                       int cell_w, int cell_h, bool center_x) {
    char str[2] = { ch, '\0' };
    SDL_Surface *surf = TTF_RenderText_Blended(font, str, state->fg);
    if (!surf) return;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect rect = {
        .x = x * cell_w + (center_x ? (cell_w - surf->w) / 2 : 0),
        .y = y * cell_h + (cell_h - surf->h) / 2,
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
    SDL_SetRenderDrawColor(renderer, state->bg.r, state->bg.g, state->bg.b, 255);
    SDL_RenderFillRect(renderer, &bg_rect);

    // Render character
    SDL_RenderCopy(renderer, tex, NULL, &rect);
    
    // Render underline
    if (state->underline) {
        SDL_Rect underline_rect = {
            .x = x * cell_w,
            .y = (y + 1) * cell_h - 2,
            .w = cell_w,
            .h = 1
        };
        SDL_SetRenderDrawColor(renderer, state->fg.r, state->fg.g, state->fg.b, 255);
        SDL_RenderFillRect(renderer, &underline_rect);
    }

    SDL_FreeSurface(surf);
    SDL_DestroyTexture(tex);
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

    GlythState state = {
        .fg = {255, 255, 255, 255},
        .bg = {0, 0, 0, 255},
        .bold = false,
        .underline = false
    };

    char *buf_ptr = term.buffer;
    char *buf_end = term.buffer + BUFSIZ - 1;

    int x = 0;
    int y = 0;
    bool in_escape = false;
    char escape_seq[16] = {0};
    int escape_idx = 0;

    while (buf_ptr < buf_end && *buf_ptr) {
        if (in_escape) {
            if (*buf_ptr == 'm' || escape_idx >= sizeof(escape_seq)-1) {
                escape_seq[escape_idx] = '\0';
                parse_sgr_sequence(escape_seq, &state);
                in_escape = false;
                escape_idx = 0;
            } else if (*buf_ptr >= '0' && *buf_ptr <= '9' || *buf_ptr == ';') {
                escape_seq[escape_idx++] = *buf_ptr;
            } else {
                in_escape = false;
            }
            buf_ptr++;
            continue;
        }

        switch (*buf_ptr) {
            case '\n':
                x = 0;
                y++;
                break;
            case '\033':  // ESC
                if (*(buf_ptr + 1) == '[') {
                    in_escape = true;
                    buf_ptr++;  // Skip '['
                    escape_idx = 0;
                }
                break;
            default:
                ui_render_char(renderer, font, *buf_ptr, &state, x, y, cell_w, cell_h, true);
                x++;
                break;
        }
        buf_ptr++;
    }

    /* Render Command Line */
    x = 0;
    y++;
    buf_ptr = term.cmd_buf;
    buf_end = term.cmd_buf + CMD_BUFSIZE - 1;
    state = (GlythState){  // Reset state for command line
        .fg = ansi_fg[7],
        .bg = ansi_bg[0],
        .bold = false,
        .underline = false
    };

    in_escape = false;
    escape_idx = 0;

    while (buf_ptr < buf_end && *buf_ptr) {
        if (in_escape) {
            if (*buf_ptr == 'm' || escape_idx >= sizeof(escape_seq)-1) {
                escape_seq[escape_idx] = '\0';
                parse_sgr_sequence(escape_seq, &state);
                in_escape = false;
                escape_idx = 0;
            } else if (*buf_ptr >= '0' && *buf_ptr <= '9' || *buf_ptr == ';') {
                escape_seq[escape_idx++] = *buf_ptr;
            } else {
                in_escape = false;
            }
            buf_ptr++;
            continue;
        }

        switch (*buf_ptr) {
            case '\n':
                x = 0;
                y++;
                break;
            case '\033':  // ESC
                if (*(buf_ptr + 1) == '[') {
                    in_escape = true;
                    buf_ptr++;  // Skip '['
                    escape_idx = 0;
                }
                break;
            default: {
                ui_render_char(renderer, font, *buf_ptr, &state, x, y, cell_w, cell_h, false);
                x++;
                
                /* Handle line wrapping */
                if ((x * cell_w) >= geometry.screen_x) {
                    x = 0;
                    y++;
                }
                break;
            }
        }
        buf_ptr++;
    }
}

size_t
term_sh_read(Term *t) {
    static char buf[4096];
    static int buflen = 0;
    ssize_t readlen;
    int written;
    UTF8Decoder decoder = t->decoder;

    readlen = read(t->sh.fd, buf + buflen, sizeof(buf) - buflen - 1);
    if (readlen < 0) {
        perror("read");
        return 0;
    } else if (readlen == 0) {
        t->sh.active = false;
        return 0;
    }

    printf("Read %ld bytes\n", readlen);
    buflen += readlen;
    buf[buflen] = '\0';  
    memcpy(t->buffer, buf, buflen);

    // if (written > 0) {
    //     buflen -= written;
    //     if (buflen > 0) {
    //         memmove(buf, buf + written, buflen);
    //     }
    // }

    return readlen;
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
