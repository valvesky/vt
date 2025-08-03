#ifndef _VT_TERM_C_
#define _VT_TERM_C_
#include "vt_term.h"

#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>


#define LL_COUNT(t) ( (t->logical_lines.write-t->logical_lines.read) / sizeof(LogicalLine))

#define ESC 27
#define DEBUG_LL_PARSER

typedef uint16_t gflags_t;
typedef uint16_t len ;

typedef uint8_t utf8_t ;


static inline void crash(const char* err);
static Shell shell_init();
static void shell_destroy(Shell *shell);

static size_t term_sh_read(Terminal *t);
static size_t term_sh_write(Terminal *t, const char * const src, size_t len);

static void term_init(Terminal *t);
static void term_destroy(Terminal *t);
static void term_scrollback_push(Terminal *term, char *str, size_t len);
static char* term_scrollback_get(term_t term, uint16_t idx);

static void term_update_logical_lines(Terminal *term);
static LogicalLine* term_ll_get_current(term_t term);

static void term_cmd_write_char(term_t term, char c);
static void term_cmd_backspace(term_t term);
static void term_cmd_left(term_t);
static void term_cmd_right(term_t);

/* the 'non-terminated' line, the last line of the circular buffer
 * is the command line where the user writes it makes sense to write
 * directly to the buffer since the prompt gets "duplicated" 
 * when you enter a command */
static LogicalLine term_ll_get_nonterminated(term_t term); 
static LogicalLine term_ll_get_last(term_t term, size_t i);
static char* term_ll_str(term_t term, LogicalLine *ll);
static int term_ll_get_visual_lines(LogicalLine ll, float cols);

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

vec4f
rgba_hex_to_vec4f(uint32_t rgba) {
  return (vec4f) {
    ((rgba>>24) & 0xFF) / 255,
    ((rgba>>16) & 0xFF) / 255,
    ((rgba>>8)  & 0xFF) / 255,
    (rgba      & 0xFF) / 255 };
}

static size_t
term_sh_read(Terminal *t) {

  /* Unfortunately seeking is not possible here
   * Yes, I tried anyways. */
  CBuffer *q = &t->scrollback;
  ssize_t r = read(t->shell.fd, q->buffer, q->buffer_size);
  q->write += r;

  printf("term_read: recieved %ld bytes\n", r);
  printf("term_read: read: %ld write: %ld\n", q->read, q->write);

  if (r < 0) {
    return 0; // Handle non-blocking
    perror("read");
    exit(EXIT_FAILURE);
  } else if (r == 0) { // EOF 
    exit(EXIT_FAILURE);
  }

  term_update_logical_lines(t);

  printf("term_read: read: %ld write: %ld\n", q->read, q->write);

  /* Read that again! */
  if (r == q->buffer_size) {
    term_sh_read(t);
  }

  return r;
}

static size_t
term_sh_write(Terminal *t, const char * const src, size_t len) {

  ssize_t r = write(t->shell.fd, src, len);
  if (r < 0) {
    perror("term_sh_write");
    _Exit(EXIT_FAILURE);
  }
}

static void
term_init(Terminal *t) {
  assert(!t->scrollback.buffer && !t->logical_lines.buffer);
  cbuffer_init(&t->scrollback, getpagesize()*3);
  cbuffer_init(&t->logical_lines, getpagesize());

  t->shell = shell_init();

  t->cursor_real = (Terminal_Cursor) {
    .bg = rgba_hex_to_vec4f(ansi_bg[0]),
    .fg = rgba_hex_to_vec4f(ansi_fg[7]),
    .x = 0,
    .y = 0,
    .flags = 0,
  };

  term_sh_read(t);
}

static void
term_destroy(Terminal *t) {
  shell_destroy(&t->shell);
  cbuffer_destroy(&t->scrollback);
  cbuffer_destroy(&t->logical_lines);
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

  LogicalLine ll = {0, 0, false, false };
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

static void
term_cmd_write_char(term_t term, char c) {

  /* The last line is never terminated
   * ------\n|[~~~@~~~]$foobar --foo=bar[] | lolcat
   *         ^         ^                ^         ^
   *  last ll+len   write ptr       cursor     cmd_len*/

  /* we dont have to push to the scrollback
   * since we already are in the scrollback */

  char* write_ptr = term_scrollback_get(term, term->scrollback.write);
  if ( (c == '\r' || c == '\n') && term->cmd_len > 0) {
    /* write prompt to scrollback */
    write_ptr[term->cmd_len++] = '\n';
    term->scrollback.write += term->cmd_len;
    term_update_logical_lines(term);

    /* write prompt to shell */
    term_sh_write(term, write_ptr, term->cmd_len);
    term_sh_read(term);
    term->cursor = 0;
    term->cmd_len = 0;
    return;
  }

  if ( (c <= 128 && c >= 32)) {
    term->cmd_len++;
    for (int i = term->cmd_len; i > term->cursor; i--) {
      write_ptr[i] = write_ptr[i-1];
    }
    write_ptr[term->cursor++] = c;
  }

}

static void
term_cmd_backspace(term_t term) {

  /* The last line is never terminated
   * ------\n|[~~~@~~~]$foobar --foo=bar[] | lolcat
   *         ^         ^                ^         ^
   *  last ll+len   write ptr       cursor     cmd_len*/

  if (term->cursor > 0) {
    char* write_ptr = term_scrollback_get(term, term->scrollback.write);

    term->cmd_len--;

    for (int i = term->cursor-1; i < term->cmd_len; i++) {
      write_ptr[i] = write_ptr[i+1];
    }

    term->cursor--;
  }
   
}

static void
term_cmd_left(term_t term) {
  if (term->cursor > 0)  
    term->cursor--;
}

static void
term_cmd_right(term_t term) {
  if (term->cursor < term->cmd_len)  
    term->cursor++;
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
  ll.len = term->cmd_len;

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


#endif
