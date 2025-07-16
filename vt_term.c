#ifndef _SCREEN_H_
#define _SCREEN_H_

#include <stdint.h>
#include "config.h"
#include "vt_array.c"
#include "vt_shell.c"

#define CMD_BUFSIZE 512

typedef struct { uint16_t x; uint16_t y;} point;
typedef point size ;
typedef point cursor ;
typedef uint16_t gflags_t;
typedef uint16_t len ;

typedef struct DrawState {
  SDL_Color fg;
  SDL_Color bg;
  gflags_t flags;
} DrawState; 

typedef struct Term {
  char cmd_buf[CMD_BUFSIZE];
  DArray text;
  DrawState draw_state;
  Shell sh;                   
  point size;
  point cursor;
  uint16_t cmd_x;
} Term;

typedef struct Term* term;

#define AREA(point) (point.x*point.y)
#define NL_FILLER 0x03L

Term term_init(size);
void term_write(term , char);
void term_cmd_write(term , char);
void term_backspace(term);
void term_clear(term);
void term_destroy(term);

/* Private Helpers */
Term
term_init(size new_size)
{
  Term new_term = {0};
  new_term.text = darray_init(sizeof(char), new_size.y*new_size.x);
  new_term.draw_state = (DrawState) {0};
  new_term.draw_state.bg = ansi_bg[0];
  new_term.draw_state.fg = ansi_fg[7];
  new_term.size = new_size;
  new_term.cursor = (point) {0,0};
  return new_term;
}

void term_cursor_back(term t) {

  cursor *c = &t->cursor;

  if (c->x == 0 && c->y == 0)
    return;

  if (c->x == 0) {
    c->x = (t->size.x-1);
    c->y--;
    return;
  }

  c->x--;
}

void term_cursor_forward(term t) {

  cursor *c = &t->cursor;

  if (c->x == t->size.x-1  && c->y == t->size.y-1)
    // add new line
    return;

  if (c->x == t->size.x-1) {
    c->x = 0;
    c->y++;
    return;
  }

  c->x++;
}

void
term_write_chr(term t, char utf8)
{ 
  if ((int) utf8 <= 128 && utf8 >= 32 ) {
    darray_insert(&t->text, &utf8);
    term_cursor_forward(t);
  }
}

// void
// term_handle_key(term t, SDL_KeyboardEvent key) {
//
//   switch(key.keysym.sym) {
//     case SDLK_RETURN:
//     case SDLK_KP_ENTER:
//       t->cmd_buf[t->cmd_x++] = '\r';
//       t->cmd_buf[t->cmd_x] = '\0';
//
//       write(t->sh.fd, t->cmd_buf, t->cmd_x);
//       term_sh_read(&t);
//
//       memset(t->cmd_buf, 0, t->cmd_x);
//       t->cmd_x = 0;
//
//       t->cursor.x = 0;
//       t->cursor.y++;
//       return;
//
//     case SDLK_BACKSPACE:
//       if (t->cmd_x > 0) {
//         t->cmd_buf[--t->cmd_x] = '\0';
//
//
//
//
//       } else if (term.cursor_x == 0) {
//         term.cmd_buf[term.cursor_x] = '\0';
//       }
//       break;
//     case SDLK_c:
//       if(SDL_GetModState() & KMOD_CTRL) {
//         printf("\n^C\n");
//         term.cursor_x = 0;
//         memset(term.cmd_buf, 0, CMD_BUFSIZE);
//       }
//       return;
//     default:
//       return;
//   }
// }
//
void
term_destroy(term sc) {
  darray_destroy(&sc->text);
  shell_destroy(&sc->sh);
}

#endif
