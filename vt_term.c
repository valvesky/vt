#pragma once

#include "vt_term.h"
#include "vt_opengl.c"

#define LL_COUNT(t) ( (t->logical_lines.write-t->logical_lines.read) / sizeof(LogicalLine))
#define ESC 27
#define DEBUG_LL_PARSER

typedef uint16_t gflags_t;
typedef uint8_t utf8_t ;

Terminal Terminal_Create(Renderer*);
void     Terminal_Destroy(Terminal*);
void     Terminal_CMD_Write(Terminal*, const char*, size_t);
void     Terminal_CMD_Backspace(Terminal*);
void     Terminal_CMD_Left(Terminal*);
void     Terminal_CMD_Right(Terminal*);
void     Terminal_CMD_Up(Terminal*);
void     Terminal_CMD_Down(Terminal*);

static inline void crash(const char* err);
static Shell shell_init();
static void shell_destroy(Shell *shell);

static size_t term_sh_read(Terminal *t);
static size_t term_sh_write(Terminal *t, const char * const src, size_t len);

static void term_scrollback_push(Terminal *term, char *str, size_t len);
static char* term_scrollback_get(term_t term, uint16_t idx);

static void term_update_logical_lines(Terminal *term);
static LogicalLine* term_ll_get_current(term_t term);

static LogicalLine term_ll_get_nonterminated(term_t term); 
static LogicalLine term_ll_get_last(term_t term, size_t i);
static char* term_ll_str(term_t term, LogicalLine *ll);
static int term_ll_get_visual_lines(LogicalLine ll, float cols);

static void term_render_ll(term_t term, LogicalLine *ll, int y_offset, int rows, int cols);

Terminal
Terminal_Create(Renderer *renderer) {

  Terminal new = {0};
  new.renderer = renderer;
  assert(new.renderer && new.renderer->cell_buffer != NULL);

  cbuffer_init(&new.scrollback, getpagesize()*3);
  cbuffer_init(&new.logical_lines, getpagesize());

  new.shell = shell_init();

  new.cursor = (Terminal_Cursor) {
    .bg = ansi_bg[0],
    .fg = ansi_fg[7],
    .x = 0,
    .y = 0,
  };

  new.state = TERM_CURSOR_CMD;

  term_sh_read(&new);

  return new;
}

void
Terminal_Destroy(Terminal *t) {
  shell_destroy(&t->shell);
  cbuffer_destroy(&t->scrollback);
  cbuffer_destroy(&t->logical_lines);
  memset(t, 0, sizeof(*t));
}

void
Terminal_CMD_Write(Terminal *term, const char *src, size_t len) {

  /* The last line is never terminated
   * ------\n|[~~~@~~~]$foobar --foo=bar[] | lolcat
   *         ^         ^                ^         ^
   *  last ll+len   write ptr       cmd_pos     cmd_len*/

  char* write_ptr = term_scrollback_get(term, term->scrollback.write);

  for (const char *end = src+len; src < end; src++) {

    if ( (*src == '\r' || *src == '\n') && term->cmd_len > 0) {

      /* write prompt to scrollback */
      write_ptr[term->cmd_len++] = '\n';
      term->scrollback.write += term->cmd_len;

      /* write prompt to shell */
      term_sh_write(term, write_ptr, term->cmd_len);
      term_sh_read(term);
      
      term_update_logical_lines(term);

      term->cmd_pos = 0;
      term->cmd_len = 0;
      return;
    }

    if (*src >= 32) {
      term->cmd_len++;
      for (int i = term->cmd_len; i > term->cmd_pos; i--) {
        write_ptr[i] = write_ptr[i-1];
      }
      write_ptr[term->cmd_pos++] = *src;
    }
  }

}

void
Terminal_CMD_Backspace(Terminal *term) {

  /* The last line is never terminated
   * ------\n|[~~~@~~~]$foobar --foo=bar[] | lolcat
   *         ^         ^                ^         ^
   *  last ll+len   write ptr       cursor     cmd_len*/

  if (term->cmd_pos > 0) {
    char* write_ptr = term_scrollback_get(term, term->scrollback.write);

    term->cmd_len--;

    for (int i = term->cmd_pos-1; i < term->cmd_len; i++) {
      write_ptr[i] = write_ptr[i+1];
    }

    term->cmd_pos--;
  }
   
}

void
Terminal_CMD_Left(Terminal *t) {
  if (t->cmd_pos > 0)  
    t->cmd_pos--;
}

void
Terminal_CMD_Right(Terminal *t) {
  if (t->cmd_pos < t->cmd_len)  
    t->cmd_pos++;
}

static inline void crash(const char* err) {
  fputs("[CRASH] ", stderr);
  perror(err);
  _Exit(EXIT_FAILURE);
}

static Shell
shell_init() {
  Shell shell = {0};

  int master, slave;
  if (openpty(&master, &slave, NULL, NULL, NULL) < 0)
    crash("couldn't open tty");
    
  shell.pid = fork();
  if (shell.pid < 0) {
    crash("fork failed");
  }

  if (shell.pid == 0) {
    close(master);
    setsid(); /* create a new process group */

    dup2(slave, STDIN_FILENO);
    dup2(slave, STDOUT_FILENO);
    dup2(slave, STDERR_FILENO);

    if (ioctl(slave, TIOCSCTTY, NULL) < 0)
      crash("ioctl failed! ");
    if (slave > STDERR_FILENO) 
      close(slave);

    setenv("TERM", "xterm-256color", 1);
    execlp("bash", "bash", "--login", NULL);

    _Exit(EXIT_SUCCESS);
  }

  shell.fd = master;
  shell.active = true;
  return shell;
}

static void
shell_destroy(Shell *shell) {
  if (!shell->active) return;

  /* Send EOF to pipe */
  if (shell->fd > 0)
    close(shell->fd); 

  /* Wait for shell to exit */
  if (shell->pid > 0)
    waitpid(shell->pid, NULL, 0);

  printf("[Shell %-d] Exited successfully\n", shell->pid);
  shell->active = false;
}

static vec4f
rgba_hex_to_vec4f(uint32_t rgba) {
  return (vec4f) {
    (float) ((rgba>>24) & 0xFF) / 255,
    (float) ((rgba>>16) & 0xFF) / 255,
    (float) ((rgba>>8)  & 0xFF) / 255,
    (float) (rgba      & 0xFF) / 255 };
}

static size_t
term_sh_read(Terminal *t) {

  CBuffer *q = &t->scrollback;
  ssize_t r = read(t->shell.fd, q->buffer+(q->write%q->buffer_size), q->buffer_size);

  printf("term_read: recieved %ld bytes\n", r);
  q->write += r;

  if (r < 0) {
    return 0; // Handle non-blocking
    perror("read");
    exit(EXIT_FAILURE);
  } else if (r == 0) { // EOF 
    exit(EXIT_FAILURE);
  }

  term_update_logical_lines(t);

  return r;
}

static size_t
term_sh_write(Terminal *t, const char * const src, size_t len) {

  ssize_t r = write(t->shell.fd, src, len);
  if (r < 0) {
    perror("term_sh_write");
    _Exit(EXIT_FAILURE);
  }

  return r;
}

static void
term_scrollback_push(Terminal *term, char *str, size_t len) {
  cbuffer_push_overwrite(&term->scrollback, str, len);
}

/* get scrollback ptr from index */
static char *
term_scrollback_get(term_t term, uint16_t idx) {
  return &term->scrollback.buffer[idx % term->scrollback.buffer_size];
}

static void
term_update_logical_lines(Terminal *term) {

  CBuffer *sc = &term->scrollback;

  /* full */
  if (sc->read == sc->write-1)
    return;

  char *ll_beg = sc->buffer + (sc->read % sc->buffer_size);
  printf("%s\n", ll_beg);
  if (*ll_beg == '\n') ll_beg++;

  char *ll_end = ll_beg+1;

  LogicalLine ll = {0, 0, 0, false, false };
  while (sc->read < sc->write-1) {

    if (ll.has_ansi == false && *ll_end == ESC) {
      ll.has_ansi = true;
    }

    if (ll.has_unicode == false && ((utf8_t) *ll_end) > 127 ) {
      ll.has_unicode = true;
    }

    if (*ll_end == '\n') {
      ll.len = ll_end - ll_beg;
      ll.start = ll_beg - sc->buffer;

#ifdef DEBUG_LL_PARSER
      fprintf(stdout, "Pushed ll = [IDX=%04ld\tLEN=%04ld\tUC=%d\tEC=%d]\n", ll.start, ll.len, ll.has_unicode, ll.has_ansi);
#endif

      cbuffer_push_overwrite(&term->logical_lines, (char*) &ll, sizeof(ll));
      ll.start = 0;
      ll.len = 0;
      ll.has_unicode = false;
      ll.has_ansi = false;

      ll_beg = ll_end+1;
    }

    ll_end++;
    sc->read++;
  }

  /* Non-terminated Line */
  sc->read = ll_beg - sc->buffer;
}

static LogicalLine*
term_ll_get_current(term_t term) {
  if (term->logical_lines.read == term->logical_lines.write) return NULL;
  return (LogicalLine*) &term->logical_lines.buffer[term->logical_lines.read];
}

static char*
term_ll_str(term_t term, LogicalLine *ll) {
  return term->scrollback.buffer+ll->start;
}

static LogicalLine 
term_ll_get_nonterminated(term_t term) {

  /* The last line is never terminated
   * ------\n|[~~~@~~~]$foobar --foo=bar[] | lolcat
   *         ^         ^                ^         ^
   *  last ll+len   write ptr       cursor     cmd_len*/

  if (LL_COUNT(term) == 0) {
    LogicalLine retv;
    retv.has_ansi = false;
    retv.has_unicode = true;
    retv.start = 0;
    retv.len = term->scrollback.write + term->cmd_len;
    return retv; 
  }

  LogicalLine ll = term_ll_get_last(term, 1);
  ll.start = ll.start+ll.len+1;
  ll.len = term->scrollback.write - ll.start + term->cmd_len;

  return ll;
}

/* get last N-ultimate logical line */
static LogicalLine
term_ll_get_last(term_t term, size_t i) {

  assert(i <= LL_COUNT(term));

  if (i == 0) return term_ll_get_nonterminated(term);

  CBuffer *cb = &term->logical_lines;
  char *write_virtual = cb->buffer+(cb->write%cb->buffer_size) + cb->buffer_size - (i)*sizeof(LogicalLine); 
  return *((LogicalLine*)write_virtual);
}

static int
term_ll_get_visual_lines(LogicalLine ll, float cols) {
  if (ll.len == 0) return 0;
  int floor = (int) cols;
  bool rem = (ll.len % floor) > 0;
  return (ll.len/floor) + rem;
}


// static Renderer_Cell*
// term_get_cell(Terminal *term, int32_t x, int32_t y) {
//   int idx = x + y*cols;
//   assert(x < (int32_t) term->renderer.term_size[0] && y < (int) term->renderer.term_size[1]);
//   return &term->renderer[idx];
// }

static void
term_render(Terminal *term, size_t virtual_line_offset) {

  int rows = renderer_get_rowsi(term->renderer);
  int cols = renderer_get_colsi(term->renderer);
  // Renderer_Cell array[rows*cols];
  size_t array_pos = 0;

  /* Get last logical line that fits on the screen */
  assert(term->renderer);

  size_t virtual_line = 0;
  int idx = 0;
  LogicalLine ll;

  while (idx <= LL_COUNT(term) && virtual_line < rows) {
    ll = term_ll_get_last(term, idx);
    virtual_line += term_ll_get_visual_lines(ll, cols);
    if (idx == LL_COUNT(term)) break;
    idx++;
  }

  /* render from the top */
  if (idx == LL_COUNT(term) && virtual_line < rows-1) {
    float floor = (float) ((int) rows);
    int y = floor-1;
    for (int i = idx; i >= 0; i--) {
      ll = term_ll_get_last(term, i);
      term_render_ll(term, &ll, y, rows, cols); 
      y -= term_ll_get_visual_lines(ll, cols);
    }
  } else {
    /* render from the bottom */
    int y = 0;
    for (int i = 0; i <= idx; i++) {
      ll = term_ll_get_last(term, i);
      y += term_ll_get_visual_lines(ll, cols);
      term_render_ll(term, &ll, y, rows, cols); 
    }
  }
}

static void 
term_render_ll(term_t term, LogicalLine *ll, int y_offset, int rows, int cols) {

  Renderer *renderer = term->renderer;
  char *text = term_scrollback_get(term, ll->start);

  int cell_index = y_offset * renderer_get_colsi(renderer);

  for (char* end = text+ll->len; text < end; text++) {
    if (*text < 32) {
      continue; 
    }

    int32_t glyth_tex = *text - 32;
    Renderer_Cell new = {
      .glyth = (cell_index<<16) & glyth_tex,
      .fg  = term->cursor.fg,
      .bg  = term->cursor.bg
    };

    Renderer_Push(renderer, new);
    cell_index += 1;
  }

}

