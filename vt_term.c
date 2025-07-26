#ifndef _VT_TERM_C_
#define _VT_TERM_C_
#include "vt_circ_buf.c"

#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"

// #include "vt_shell.c"

typedef struct { uint16_t x, y;} point;
typedef point size ;
typedef point cursor ;
typedef uint16_t gflags_t;
typedef uint16_t len ;
typedef uint16_t idx;
typedef uint8_t utf8_t ;

typedef struct DrawState {
  SDL_Color fg;
  SDL_Color bg;
  gflags_t flags;
} DrawState; 

typedef struct Term Term;

typedef struct LL {
  uint64_t start;
  uint64_t len;
  bool has_unicode;
  bool has_draw_codes;
} LogicalLines;


struct Term {
  CBuffer scrollback;    // main circular buffer with input from shell
  CBuffer logical_lines; // circular buffer with indexes of logical lines 
  idx cols;
  idx rows;

  // idx lines[];

  // char cmd_buf[CMD_BUFSIZE];
  // DrawState draw_state;
  // Shell sh;                   
  // point size;
  // point cursor;
  // uint16_t cmd_x;

  DrawState draw_state;
  point cursor;
  uint16_t cmd_x;
};

typedef struct Term* term_t;

#define AREA(point) (point.x*point.y)
#define NL_FILLER 0x03L

void term_init(Term *t, uint16_t cols, uint16_t rows);
void term_destroy(Term *t);
void term_cmd_write(term_t , utf8_t);
void term_scrollback_push(Term *term, char *str, size_t len);
void term_parse_logical_lines(Term *term);

LogicalLines term_ll_get_last(term_t term, uint16_t i);
char* term_ll_str(term_t term, LogicalLines *ll);


void term_init(Term *t, uint16_t cols, uint16_t rows) {
  assert(!t->scrollback.buffer && !t->logical_lines.buffer);
  cbuffer_init(&t->scrollback, getpagesize()*3);
  cbuffer_init(&t->logical_lines, getpagesize());
  t->cols = cols;
  t->rows = rows;

  t->draw_state = (DrawState) {0};
  t->draw_state.bg = ansi_bg[0];
  t->draw_state.fg = ansi_fg[7];
  t->cursor = (point) {0,0};
}

void
term_destroy(Term *t) {
  cbuffer_destroy(&t->scrollback);
  cbuffer_destroy(&t->logical_lines);
}


void term_cursor_back(term_t t) {
  cursor *c = &t->cursor;
  if (c->x == 0 && c->y == 0)
    return;
  if (c->x == 0) {
    c->x = (t->cols-1);
    c->y--;
    return;
  }
  c->x--;
}

void term_cursor_forward(term_t t) {

  cursor *c = &t->cursor;

  if (c->x == t->cols-1  && c->y == t->rows-1)
    // add new line
    return;

  if (c->x == t->cols-1) {
    c->x = 0;
    c->y++;
    return;
  }

  c->x++;
}

void
term_cmd_write(term_t t, utf8_t c) { 
  if ( (c <= 128 && c >= 32) || c == '\n') {
    term_scrollback_push(t, (char*) &c, 1);
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


void
term_scrollback_push(Term *term, char *str, size_t len) {
  cbuffer_push_overwrite(&term->scrollback, str, len);
}

void
term_parse_logical_lines(Term *term) {
  /* this pushes the write forward 
   * and parses lines */

  CBuffer *sc = &term->scrollback;

  /* full */
  if (sc->read == sc->write-1)
    return;

  char *ll_beg = sc->buffer + (sc->read % sc->buffer_size);
  char *ll_end = ll_beg+1;
  
  LogicalLines ll = {0, 0, false, false };
  while (sc->read < sc->write-1) {

    if (ll.has_unicode == false && ((utf8_t) *ll_end) > 127 ) {
      ll.has_unicode = true;
    }

    if (*ll_end == '\n') {
      ll.len = ll_end - ll_beg;
      ll.start = ll_beg - sc->buffer;

#ifdef DEBUG_LL_PARSER
    fprintf(stdout, "ll = [IDX=%04ld\tLEN=%04ld\tUC=%d\tEC=%d]\n", ll.start, ll.len, ll.has_unicode, ll.has_draw_codes);
#endif

      cbuffer_push_overwrite(&term->logical_lines, (char*) &ll, sizeof(ll));
      ll.start = 0;
      ll.len = 0;
      ll.has_unicode = false;
      ll.has_draw_codes = false;

      ll_beg = ll_end+1;
    }

    ll_end++;
    sc->read++;
  }

  sc->read = sc->write-1;
}

/* This function takes the logical line buffer
 * and displays it in stdout, mostly for debugging purposes  */
void
term_print(Term *t) {

  CBuffer *ll_buf = &t->logical_lines;
  CBuffer *sc = &t->scrollback;

  LogicalLines *ll;
  while (NULL != (ll = (LogicalLines*) cbuffer_read(ll_buf, sizeof(*ll)))) {

    size_t visual_lines = ll->len/t->cols;
    char *ptr = &sc->buffer[ll->start];
    char *end = ptr + t->cols;

    char temp;
    for (size_t i = 0; i < visual_lines; i++) {
      end = ptr + t->cols;

      temp = *end;
      *end = '\0';
      if (ll->has_unicode == false)
        printf("|%s|\n", ptr);
      *end = temp;

      ptr += t->cols;
    }

    size_t remainder = (ll->len % t->cols);
    temp = *(end+remainder);
    *(end+remainder) = '\0';
    printf("|%-*s|\n", t->cols, end);
    *(end+remainder) = temp;
  }
}

/* get scrollback ptr from index */
char *term_sc_get_idx(term_t term, uint16_t idx) {
  return &term->scrollback.buffer[idx % term->scrollback.buffer_size];
}


char*
term_ll_str(term_t term, LogicalLines *ll) {
  return term->scrollback.buffer+ll->start;
}

#define LL_COUNT(t) ( (t->logical_lines.write-t->logical_lines.read) / sizeof(LogicalLines))

static LogicalLines 
term_ll_get_nonterminated(term_t term) {
  /* The last line is never terminated
   * |------\n|------\n|$~~~~~~W 
   *                   ^       ^
   *          last ll + len    write ptr*/

  if (LL_COUNT(term) == 0) {
    LogicalLines retv;
    retv.has_draw_codes = false;
    retv.has_unicode = true;
    retv.start = 0;
    retv.len = term->scrollback.write;
    return retv; 
  }

  LogicalLines ll = term_ll_get_last(term, 1);
  ll.start = ll.start+ll.len+1;
  ll.len = term->scrollback.write - ll.start;

  return ll;
}

/* get last N-ultimate logical line */
LogicalLines
term_ll_get_last(term_t term, uint16_t i) {

  assert(i <= LL_COUNT(term));

  if (i == 0) return term_ll_get_nonterminated(term);

  CBuffer *cb = &term->logical_lines;
  char *write_virtual = cb->buffer+(cb->write%cb->buffer_size) + cb->buffer_size - (i)*sizeof(LogicalLines); 
  return *((LogicalLines*)write_virtual);
}


#endif
