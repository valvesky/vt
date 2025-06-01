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

typedef enum {
    SEQ_NONE,
    SEQ_CSI,  // ESC [ … <final>
    SEQ_OSC,  // ESC ] … BEL
    SEQ_DCS   // ESC P … ESC
} Sequence;

typedef struct {
  int pid;
  int fd;
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
    SDL_Color fg; // 4 bytes
    SDL_Color bg; // 4 bytes
    wchar_t utf8; // 4 bytes
    bool bold;      // TODO: use flags instead
    bool underline;
} Glyth;          // 16 bytes

#define MAX_COLS 256
#define MAX_ROWS 256

typedef Glyth Line[MAX_COLS];

typedef struct {
  Line *lines;
  char cmd_buf[CMD_BUFSIZE];  // 512 bytes
  Shell sh;                   // 12 bytes (4 byte alignment)
  UTF8Decoder decoder;        // 8 bytes  (4 byte alignment)
  uint16_t buf_pos;           // 2 bytes
  uint16_t cmd_cursor_x;      // 2 bytes
  uint16_t cmd_cursor_y;      // 2 bytes
  uint16_t nlines;
} Term;

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

Shell shell_init();
void shell_destroy(Shell *shell);

size_t term_sh_read(Term *t);

static const float alpha = 0.7;

static inline void crash(const char* err) {
  fputs("[CRASH] ", stderr);
  perror(err);
  _Exit(EXIT_FAILURE);
}

#endif
