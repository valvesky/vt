#ifndef _VT_H_
#define _VT_H_

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* SDL libraries */
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_events.h>

#include <locale.h>
#include <stdlib.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <wait.h>

#define CMD_BUFSIZE 512

typedef struct {
  int pid;
  int fd;
  // int stdin_fd;
  // int stdout_fd;
  // int stderr_fd;
  bool active;
} Shell;

typedef struct {
  int screen_x;
  int screen_y;
} Geometry;

typedef struct {
    wchar_t partial_char;
    int bytes_remaining;
} UTF8Decoder;

typedef struct {
  char buffer[BUFSIZ];
  char cmd_buf[CMD_BUFSIZE];
  uint16_t cmd_buf_pos;
  uint16_t cmd_cursor_pos;
  Shell sh;
  UTF8Decoder decoder;
} Term;

typedef struct {
    SDL_Color fg;
    SDL_Color bg;
    bool bold;
    bool underline;
} GlythState;

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
  {0, 0, 0, 255},       // 40: Black
  {128, 0, 0, 255},     // 41: Red
  {0, 128, 0, 255},     // 42: Green
  {128, 128, 0, 255},   // 43: Yellow
  {0, 0, 128, 255},     // 44: Blue
  {128, 0, 128, 255},   // 45: Magenta
  {0, 128, 128, 255},   // 46: Cyan
  {200, 200, 200, 255}  // 47: White
};

Shell shell_init();
void shell_destroy(Shell *shell);

size_t term_sh_read(Term *t);

static inline void crash(const char* err) {
  fputs("[CRASH] ", stderr);
  perror(err);
  _Exit(EXIT_FAILURE);
}

#endif
