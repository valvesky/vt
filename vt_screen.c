#ifndef _SCREEN_H_
#define _SCREEN_H_

#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>

#include "config.h"
#include "vt_array.c"

typedef struct { uint16_t x; uint16_t y; } point;
typedef point size;
typedef uint16_t gflags_t;
typedef uint16_t len ;

typedef struct {
  SDL_Color fg;
  SDL_Color bg;
  wchar_t codepoint;
  gflags_t flags;
} Glyth; 

typedef struct Screen {
  DArray lines;
  size size;
  point cursor;
} Screen;

typedef struct Screen* screen;

#define AREA(point) (point.x*point.y)
#define NL_FILLER 0x03L

Screen screen_init(size);
void screen_resize(screen, size);
void screen_clearline(screen, point);
void screen_write_chr(screen , wchar_t);
void screen_backspace(screen);
void screen_set_style(screen, point, gflags_t);
void screen_set_cursor(screen, point);
void screen_clear(screen);
void screen_destroy(screen);

/* Private Helpers */
// static Glyth* get_cell(screen, point);
// static Glyth* get_cursor(screen);

Screen
screen_init(size new_size)
{
  Screen new_screen = {0};
  new_screen.lines = darray_init(sizeof(struct DArray), new_size.y);

  for (size_t p = 0; p < new_size.x; p++) {
    darray_get(new_screen.lines, 3);

    ptr->next = line_init(new_size.y);
    ptr->next->prev = ptr;


    new_screen.line[p] = (Glyth) {0};
    new_screen.matrix[p].bg = ansi_bg[0];
    new_screen.matrix[p].fg = ansi_fg[7];
  }

  new_screen.size = new_size;
  new_screen.cursor = (point) {0,0};
  return new_screen;
}


void
screen_resize(screen sc, size new_size)
{ 
  Screen new_screen = screen_init(new_size);

  for (int p = 0; p < AREA(sc->size); p++) {
    if (sc->matrix[p].codepoint == 0L) break;

    /* consume filler sequence */
    if (sc->matrix[p].codepoint == NL_FILLER) {
      while (sc->matrix[p].codepoint == NL_FILLER) p++;

      if (p < AREA(sc->size)) {
        screen_write_chr(&new_screen, '\n');
      }
      // continue;
    }

    printf("%c", sc->matrix[p].codepoint);
    screen_write_chr(&new_screen, sc->matrix[p].codepoint);
  }

  printf("\n");
  free(sc->matrix);
  sc->matrix = new_screen.matrix;
  sc->size = new_screen.size;
  sc->cursor = new_screen.cursor;
}

void
screen_write_chr(screen sc, wchar_t unicode)
{ 
  if (unicode == '\n') {
    int newline_skip = sc->size.x - sc->cursor.x;
    Glyth* ptr = get_cursor(sc);

    while (newline_skip > 0) {
      ptr->codepoint = NL_FILLER;
      newline_skip--;
      ptr++;
      sc->cursor.x++;
    }

    sc->cursor.y++;
    return;
  }

  get_cursor(sc)->codepoint = unicode;
  if (sc->cursor.x < sc->size.x-1) {
    sc->cursor.x++;
  } else if (sc->cursor.y < sc->size.y-1) {
    sc->cursor.x = 0;
    sc->cursor.y++;
  } else {
    /* Last letter */
    // sc->cursor.x = 0;
    // sc->cursor.y++;
  }
}

// void
// screen_write_str(screen sc, const char* str, uint16_t len)
// { 
//
// }

// static void
// screen_add_line(screen sc) {
//   Glyth *new_matrix = 
//   if (new_matrix) {
//     sc->matrix = new_matrix;
//   }
// }

void
screen_backspace(screen sc)
{
  get_cursor(sc)->codepoint = L'\0';
  if (sc->cursor.x > 0) {
    sc->cursor.x--;
  } else if (sc->cursor.y > 0) {
    sc->cursor.x = sc->size.x-1;
    sc->cursor.y--;
  }
};

void screen_set_style(screen, point, gflags_t);
void screen_set_cursor(screen, point);

void
screen_clear(screen sc)
{
  memset(sc->matrix, 0, sizeof(Glyth) * AREA(sc->size) );
}

void
screen_destroy(screen sc) 
{
  free(sc->matrix);
}

#endif
