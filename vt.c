#define _GNU_SOURCE // has to be at the top                                                                                                                                             
/*
 *                  _/
 *   _/      _/  _/_/_/_/
 *  _/      _/    _/
 *   _/  _/      _/
 *    _/          _/_/ 
 *
 */

// #pragma GCC poison malloc
// #pragma GCC poison free

#define STB_TRUETYPE_IMPLEMENTATION
#include "lib/stb_truetype.h"

/* SDL3 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_video.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <pty.h>

#include "config.h"
#include "vt_circ_buf.c"
#include "vt.h"

#ifdef _VT_OPENGL 
#include "opengl/glad-3.3.c"
#include <SDL3/SDL_opengl.h>
#include "vt_renderer_opengl.c"
#else
#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>
#include "vt_renderer_vulkan.c"
#endif

Screen screen = {0};
Renderer renderer = {0};
Terminal vt = {0};

void screen_init(Screen* screen, uint16_t cols, uint16_t rows);
void screen_destroy(Screen *screen);

void
screen_init(Screen* screen, uint16_t cols, uint16_t rows)
{
  assert(!screen->cell_buffer);

  screen->cell_buffer = calloc(rows*cols, sizeof(*screen->cell_buffer));
  screen->cols = cols;
  screen->rows = rows;
}

void
screen_destroy(Screen *screen)
{
  if (screen->cell_buffer) free(screen->cell_buffer);
  memset(screen, 0, sizeof(*screen));
}

#define LL_COUNT(t) ( (t->logical_lines.write-t->logical_lines.read) / sizeof(LogicalLine))
#define ESC 27
#define DEBUG_LL_PARSER

typedef uint8_t utf8_t;

/* API */
static bool terminal_init(Terminal*, Screen*);
static void terminal_destroy(Terminal*);

void terminal_cmd_write(Terminal*, const char*, size_t);
// static void     terminal_cmd_backspace(terminal*);
void terminal_cmd_left(Terminal*);
void terminal_cmd_right(Terminal*);
// static void     Terminal_CMD_Up(Terminal*);
// static void     Terminal_CMD_Down(Terminal*);

inline void crash(const char* err);
Shell shell_init();
void shell_destroy(Shell *shell);
size_t term_sh_read(Terminal *t);
size_t term_sh_write(Terminal *t, const char * const src, size_t len);
void term_scrollback_push(Terminal *term, char *str, size_t len);
char* term_scrollback_get(Terminal *term, uint16_t idx);
void term_update_logical_lines(Terminal *term);
LogicalLine* term_ll_get_current(Terminal *term);
LogicalLine term_ll_get_nonterminated(Terminal *term); 
LogicalLine term_ll_get_last(Terminal *term, size_t i);
char* term_ll_str(Terminal *term, LogicalLine *ll);
int term_ll_get_visual_lines(LogicalLine ll, float cols);
void term_render_ll(Terminal *term, LogicalLine *ll, int y_offset);

static bool
terminal_init(Terminal *t, Screen *screen) {
  assert(screen && screen->cell_buffer);

  Terminal new = {0};

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
  new.screen = screen;

  *t = new;

  // term_sh_read(&new);

  return true;
}

static void
terminal_destroy(Terminal *t) {
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

void crash(const char* err) {
  fputs("[CRASH] ", stderr);
  perror(err);
  _Exit(EXIT_FAILURE);
}

Shell
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

void
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

size_t
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

size_t
term_sh_write(Terminal *t, const char * const src, size_t len) {

  ssize_t r = write(t->shell.fd, src, len);
  if (r < 0) {
    perror("term_sh_write");
    _Exit(EXIT_FAILURE);
  }

  return r;
}

void
term_scrollback_push(Terminal *term, char *str, size_t len) {
  cbuffer_push_overwrite(&term->scrollback, str, len);
}

/* get scrollback ptr from index */
char *
term_scrollback_get(Terminal *term, uint16_t idx) {
  return &term->scrollback.buffer[idx % term->scrollback.buffer_size];
}

void
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

LogicalLine*
term_ll_get_current(Terminal *term) {
  if (term->logical_lines.read == term->logical_lines.write) return NULL;
  return (LogicalLine*) &term->logical_lines.buffer[term->logical_lines.read];
}

char*
term_ll_str(Terminal *term, LogicalLine *ll) {
  return term->scrollback.buffer+ll->start;
}

LogicalLine 
term_ll_get_nonterminated(Terminal *term) {

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
LogicalLine
term_ll_get_last(Terminal *term, size_t i) {

  assert(i <= LL_COUNT(term));

  if (i == 0) return term_ll_get_nonterminated(term);

  CBuffer *cb = &term->logical_lines;
  char *write_virtual = cb->buffer+(cb->write%cb->buffer_size) + cb->buffer_size - (i)*sizeof(LogicalLine); 
  return *((LogicalLine*)write_virtual);
}

int
term_ll_get_visual_lines(LogicalLine ll, float cols) {
  if (ll.len == 0) return 0;
  int floor = (int) cols;
  bool rem = (ll.len % floor) > 0;
  return (ll.len/floor) + rem;
}


void
term_render(Terminal *term, size_t virtual_line_offset) {

  size_t cols = term->screen->cols;
  size_t rows = term->screen->rows;

  size_t virtual_line = 0 * virtual_line_offset;
  size_t idx = 0;
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
    for (size_t i = idx; i > 0; --i) {
      ll = term_ll_get_last(term, i);
      term_render_ll(term, &ll, y); 
      y -= term_ll_get_visual_lines(ll, cols);
    }
  } else {
    /* render from the bottom */
    int y = 0;
    for (size_t i = 0; i <= idx; i++) {
      ll = term_ll_get_last(term, i);
      y += term_ll_get_visual_lines(ll, cols);
      term_render_ll(term, &ll, y); 
    }
  }
}

void 
term_render_ll(Terminal *term, LogicalLine *ll, int y_offset) {

  char *text = term_scrollback_get(term, ll->start);
  size_t idx = term->screen->cols * y_offset;

  Terminal_Cell *dest = term->screen->cell_buffer+idx; 

  for (char* end = text+ll->len; text < end; text++) {
    if (*text < 32) { continue; }

    dest->codepoint = *text;
    dest->is_dirty = true;
    dest->fg  = term->cursor.fg;
    dest->bg  = term->cursor.bg;
  };

}

int main() {

  screen_init(&screen, 140, 40);

  if (!renderer_init(&renderer, &screen)) {
    screen_destroy(&screen);
    return 1;
  }

  if (!terminal_init(&vt, &screen)) {
    renderer_destroy(&renderer);
    screen_destroy(&screen);
    return 1;
  }

  screen.cell_buffer[0].bg = ansi_bg[BLACK];
  screen.cell_buffer[0].fg = ansi_fg[RED];
  screen.cell_buffer[0].codepoint = 'V';
  screen.cell_buffer[0].is_dirty = true;

  screen.cell_buffer[1].codepoint = 'T';
  screen.cell_buffer[1].bg = ansi_bg[BLACK];
  screen.cell_buffer[1].fg = ansi_fg[RED];
  screen.cell_buffer[1].is_dirty = true;

  bool redraw = true;
  bool running = true;
  SDL_Event event;
  while (running) {
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_EVENT_WINDOW_RESIZED:
          renderer_resize_screen(&renderer, event.display.data1, event.display.data2);
          redraw = true;
          break;
        case SDL_EVENT_QUIT:
          running = false;
      }
    }

    if (redraw) {
      renderer_draw_screen(&renderer);
      redraw = false;
    }

    SDL_Delay(12);
  }

  terminal_destroy(&vt);
  renderer_destroy(&renderer);
  screen_destroy(&screen);
  puts("Quit successfully!");
  return 0;
}

