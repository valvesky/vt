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
#include <limits.h>
#include <sys/time.h>

/* debugging is good for you */
#ifdef DEBUG
#define LOG_WARN_ENABLED 1
#define LOG_INFO_ENABLED 1
#define LOG_DEBUG_ENABLED 1
#define LOG_TRACE_ENABLED 1
#else
#define LOG_WARN_ENABLED  0
#define LOG_INFO_ENABLED  0
#define LOG_DEBUG_ENABLED 0
#define LOG_TRACE_ENABLED 0
#endif
#include "vt_debug.h"

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

#define BEL '\x07'
#define ESC '\x1B'

typedef uint32_t u32;

/* Arbitrary sizes */
#define UTF_INVALID   0xFFFD
#define UTF_SIZ       4
#define ESC_ARG_SIZ   16
#define STR_ARG_SIZ   ESC_ARG_SIZ

/* Flags */
#define VT_IS_SET(flag)    ((vt.state & (flag)) != 0)
#define VT_SET(flag)       (vt.state |= (flag))
#define VT_UNSET(flag)     (vt.state &= ~(flag))

#define LL_COUNT ((vt.logical_lines.write - vt.logical_lines.read) / sizeof(LogicalLine))

/* NOTE: I just want to credit ST here because I basically
 * copied what it does when it comes to parsing sequences but
 * managed to implement it without copying data. */
enum Escape_State {
	ESC_START      = 1,
	ESC_CSI        = 2,
	ESC_STR        = 4,  
	ESC_ALTCHARSET = 8,
	ESC_STR_END    = 16, 
	ESC_TEST       = 32, 
	ESC_UTF8       = 64,
};

typedef struct {
  char priv;
  int arg[ESC_ARG_SIZ];
  int narg; /* number of args */
  char mode[2];
} CSIEscape;

/* --- Variables --- */
static CSIEscape csi_escape_seq = {0};
static Terminal vt = {0};
static Screen screen = {0};
static Renderer renderer = {0};
static struct timeval start, end;
static bool redraw = true;
static bool running = true;
static uint64_t frames = 0;

/* --- Function Declarations --- */
static bool vt_init(Screen*);
static void vt_destroy(void);
static u32  vt_parse_csi(const char *ptr, const char * const end);
static void vt_handle_csi(void);
static u32  vt_parse_osc(const char *ptr, const char * const end);
static void vt_render_naive(void);

static void screen_init(uint16_t cols, uint16_t rows);
static void screen_destroy(void);

static void vt_cmd_write(const char *src, size_t len);
static void vt_cmd_backspace(void);
static void vt_cmd_left(void);
static void vt_cmd_right(void);

static void crash(const char* err);
static Shell shell_init();
static void shell_destroy(Shell *shell);

static size_t vt_sh_read();
static size_t vt_sh_write(const char * const src, size_t len);

static void vt_scrollback_push(char *str, size_t len);
static char* vt_scrollback_get(uint16_t idx);

static void vt_update_logical_lines(void);
static LogicalLine* vt_ll_get(void);
static LogicalLine vt_ll_get_nonterminated(void); // fetches the prompt

static LogicalLine vt_ll_get_last(size_t i);
static int vt_ll_get_visual_lines(LogicalLine ll, float cols);



/* --- Function Definitions --- */

static bool
vt_init(Screen *screen) 
{
  assert(screen && screen->cell_buffer);

  cbuffer_init(&vt.scrollback, getpagesize()*3);
  cbuffer_init(&vt.logical_lines, getpagesize());

  vt.shell = shell_init();
  vt.cursor = (Terminal_Cursor) {
    .bg = ansi_bg[BLACK],
    .fg = ansi_fg[WHITE],
    .x = 0, .y = 0,
  };

  vt.state = 0;
  vt.screen = screen;

  vt_sh_read();
  return true;
}

static void
vt_destroy() 
{
  shell_destroy(&vt.shell);
  cbuffer_destroy(&vt.scrollback);
  cbuffer_destroy(&vt.logical_lines);
  memset(&vt, 0, sizeof vt);
}

static uint32_t 
vt_parse_csi(const char *ptr, const char * const end) 
{
  assert(VT_IS_SET(ESC_START) && VT_IS_SET(ESC_CSI));

  VTDEBUG("Parsing CSI...");

  const char *start = ptr;
  char *np;
	long int v;

	csi_escape_seq.narg = 0;
	if (*ptr == '?') { // VT codes
		csi_escape_seq.priv = 1;
		ptr++;
	}

  for (; ptr < end; ptr++) {
    np = NULL;
    v = strtol(ptr, &np, 10);
    if (np == ptr)
      v = 0;
    if (v == LONG_MAX || v == LONG_MIN)
      v = -1;
    csi_escape_seq.arg[csi_escape_seq.narg++] = v;
    ptr = np;
    if (*ptr != ';' || csi_escape_seq.narg == ESC_ARG_SIZ)
      break;
  }
  
  if (*ptr < 0x40 || *ptr > 0x7E) {
    VTWARN("vt_parse_csi: invalid final byte: 0x%02x", *ptr);
    return ptr - start;
  }

  csi_escape_seq.mode[0] = *ptr++;
  csi_escape_seq.mode[1] = (ptr < end) ? *ptr : '\0';
  vt_handle_csi();

  VT_UNSET(ESC_CSI);
  return ptr - start;
}

static void
vt_handle_csi(void)
{
}

static u32
vt_parse_osc(const char *ptr, const char * const end) {
// ESC ]0;this is the window title BEL
  assert(VT_IS_SET(ESC_START) && VT_IS_SET(ESC_CSI));
  const char *start = ptr;

  if (*ptr == ']') ptr++;

  for (; ptr < end; ptr++) {
    if (*ptr == '0') {
      // SDL_SetWindowTitle();
    }
    if (*ptr == BEL) {
      return ptr - start;
    }
  }

  VTWARN("Did not reach end of OSC sequence");
  return ptr - start;
}

static void
vt_render_naive(void) 
{

  Screen *screen = vt.screen;
  Terminal_Cell *screen_cell = &screen->cell_buffer[vt.cursor.y *screen->cols + vt.cursor.x] ;

  LogicalLine *consume = NULL;

  do {
    consume = vt_ll_get();
    char *ptr;
    char *end;
    bool parse_escape = false;

    if (consume) {
      ptr = vt_scrollback_get(consume->start);
      end = ptr + consume->len;
      parse_escape = consume->has_ansi;
    } else {
      /* I consider the prompt to be a "non terminated" logical line */
      LogicalLine ll = vt_ll_get_nonterminated();
      ptr = vt_scrollback_get(ll.start);
      end = ptr + ll.len;
      parse_escape = ll.has_ansi;
    }

    if (parse_escape)
      goto parse_ll_with_codes;
    else
      goto parse_ll_ignore_codes;

    /* Logical Line, non escape sequence path */
parse_ll_ignore_codes:
    VTDEBUG("Ignoring codes");
    for (; ptr < end; ptr++) {
      screen_cell->codepoint = *ptr;
      screen_cell->is_dirty = true;
      screen_cell->bg = vt.cursor.bg;
      screen_cell->fg = vt.cursor.fg;
      screen_cell++;
    }

    goto parse_ll_next_line;

    /* Logical Line with escape sequence path */
parse_ll_with_codes:
    VTDEBUG("Not ignoring codes");
    for (; ptr < end; ptr++) {
      
      if (!VT_IS_SET(ESC_START)) { // not in escape sequence
        if (*ptr < 32) { // not visible ASCII
          if (*ptr == ESC) { // escape sequence start
            VT_SET(ESC_START);
            VTDEBUG("Escape Sequence Detected");
            continue;
          }
          
          // TODO: parse utf8 here
          // do nothing for now

        } else { // not escape sequence AND visible ASCII
          screen_cell->codepoint = *ptr;
          screen_cell->is_dirty = true;
          screen_cell->bg = vt.cursor.bg;
          screen_cell->fg = vt.cursor.fg;
          screen_cell++;
        }
      } else { // in escape sequence
        if (*ptr == '[') {
          VT_SET(ESC_CSI);
          ptr += vt_parse_csi(ptr, end);
          VT_UNSET(ESC_START);
        } else if (*ptr == ']') {
          VT_SET(ESC_CSI);
          ptr += vt_parse_osc(ptr, end);
          VT_UNSET(ESC_START);
        } else {
          VTWARN("Unknown escape sequence initializer %c", *ptr);
        }
      }
    }

parse_ll_next_line:

    /* we can reverse engineer the cursor position 
     * from the screen pointer */
    size_t idx = screen_cell - screen->cell_buffer;
    vt.cursor.x = idx % screen->cols;    
    vt.cursor.y = idx / screen->cols;

    /* state is reset after every logical line */
    vt.state = 0;

    if (vt.cursor.y < screen->rows)
      vt.cursor.y++;

  } while (consume);
}


static void
screen_init(uint16_t cols, uint16_t rows)
{
  assert(!screen.cell_buffer);
  screen.cell_buffer = calloc(rows*cols, sizeof *screen.cell_buffer);
  screen.cols = cols;
  screen.rows = rows;
}

static void
screen_destroy(void)
{
  if (screen.cell_buffer) free(screen.cell_buffer);
  memset(&screen, 0, sizeof screen);
}

static void
vt_cmd_write(const char *src, size_t len) 
{

  /* The last line is never terminated
   * ------\n|[~~~@~~~]$foobar --foo=bar[] | lolcat
   *         ^         ^                ^         ^
   *  last ll+len   write ptr       cmd_pos     cmd_len*/

  char* write_ptr = vt_scrollback_get(vt.scrollback.write);

  for (const char *end = src+len; src < end; src++) {

    if ( (*src == '\r' || *src == '\n') && vt.cmd_len > 0) {

      /* write prompt to scrollback */
      write_ptr[vt.cmd_len++] = '\n';
      vt.scrollback.write += vt.cmd_len;

      /* write prompt to shell */
      vt_sh_write(write_ptr, vt.cmd_len);
      vt_sh_read();
      vt_update_logical_lines();

      vt.cmd_pos = 0;
      vt.cmd_len = 0;
      return;
    }

    if (*src >= 32) {
      vt.cmd_len++;
      for (int i = vt.cmd_len; i > vt.cmd_pos; i--) {
        write_ptr[i] = write_ptr[i-1];
      }
      write_ptr[vt.cmd_pos++] = *src;
    }
  }

}

static void
vt_cmd_backspace(void)
{
  /* The last line is never terminated
   * ------\n|[~~~@~~~]$foobar --foo=bar[] | lolcat
   *         ^         ^                ^         ^
   *  last ll+len   write ptr       cursor     cmd_len*/

  if (vt.cmd_pos > 0) {
    char* write_ptr = vt_scrollback_get(vt.scrollback.write);

    vt.cmd_len--;

    for (int i = vt.cmd_pos-1; i < vt.cmd_len; i++) {
      write_ptr[i] = write_ptr[i+1];
    }

    vt.cmd_pos--;
  }
   
}

static void
vt_cmd_left(void) 
{
  if (vt.cmd_pos > 0)  
    vt.cmd_pos--;
}

static void
vt_cmd_right(void)
{
  if (vt.cmd_pos < vt.cmd_len)  
    vt.cmd_pos++;
}

static void
crash(const char* err)
{
  VTFATAL("%s", err);
  _Exit(EXIT_FAILURE);
}

static Shell
shell_init()
{
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
shell_destroy(Shell *shell) 
{
  if (!shell->active) return;

  /* Send EOF to pipe */
  if (shell->fd > 0)
    close(shell->fd); 

  /* Wait for shell to exit */
  if (shell->pid > 0)
    waitpid(shell->pid, NULL, 0);

  VTINFO("[Shell %-d] Exited successfully", shell->pid);
  shell->active = false;
}

static size_t
vt_sh_read() 
{
  CBuffer *q = &vt.scrollback;
  ssize_t r = read(vt.shell.fd, q->buffer+(q->write%q->buffer_size), q->buffer_size);

  if (r < 0) {
    return 0; // Handle non-blocking
    VTFATAL("vt_sh_read");
    exit(EXIT_FAILURE);
  } else if (r == 0) { // EOF 
    exit(EXIT_FAILURE);
  }

  VTDEBUG("vt_read: recieved %ld bytes", r);
  q->write += r;

  vt_update_logical_lines();

  return r;
}

static size_t
vt_sh_write(const char * const src, size_t len) 
{
  ssize_t r = write(vt.shell.fd, src, len);
  if (r < 0)
    crash("vt_sh_write");
  return r;
}

static void
vt_scrollback_push(char *str, size_t len) 
{
  cbuffer_push_overwrite(&vt.scrollback, str, len);
}

static  char *
vt_scrollback_get(uint16_t idx) 
{
  return &vt.scrollback.buffer[idx % vt.scrollback.buffer_size];
}

static void
vt_update_logical_lines(void) 
{
  CBuffer *sc = &vt.scrollback;

  /* full */
  if (sc->read == sc->write-1)
    return;

  char *ll_beg = sc->buffer + (sc->read % sc->buffer_size);
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

      VTDEBUG("Pushed ll = [IDX=%04ld\tLEN=%04ld\tUC=%d\tEC=%d]", ll.start, ll.len, ll.has_unicode, ll.has_ansi);

      cbuffer_push_overwrite(&vt.logical_lines, (char*) &ll, sizeof(ll));
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
vt_ll_get(void) 
{
  return (LogicalLine*) cbuffer_read(&vt.logical_lines, sizeof(LogicalLine) );
}

static LogicalLine 
vt_ll_get_nonterminated(void) 
{
  /* The last line is never terminated
   * ------\n|[~~~@~~~]$foobar --foo=bar[] | lolcat
   *         ^         ^                ^         ^
   *  last ll+len   write ptr       cursor     cmd_len*/

  if (LL_COUNT == 0) {
    LogicalLine retv;
    retv.has_ansi = true;  // always assume the worst
    retv.has_unicode = true;
    retv.start = 0;
    retv.len = vt.scrollback.write + vt.cmd_len;
    return retv; 
  }

  LogicalLine ll = vt_ll_get_last(1);
  ll.start = ll.start+ll.len+1;
  ll.len = vt.scrollback.write - ll.start + vt.cmd_len;
  ll.has_ansi = true;  
  ll.has_unicode = true;

  return ll;
}

/* get last N-ultimate logical line */
static LogicalLine
vt_ll_get_last(size_t i)
{
  assert(i <= LL_COUNT);

  if (i == 0) return vt_ll_get_nonterminated();

  CBuffer *cb = &vt.logical_lines;
  char *write_virtual = cb->buffer+(cb->write%cb->buffer_size) + cb->buffer_size - (i)*sizeof(LogicalLine); 
  return *((LogicalLine*)write_virtual);
}

static int
vt_ll_get_visual_lines(LogicalLine ll, float cols) 
{
  // TODO: make this work for escape codes and 
  // unicode so i can implement the non-naive uber parser
  if (ll.len == 0) return 0;
  int floor = (int) cols;
  bool rem = (ll.len % floor) > 0;
  return (ll.len/floor) + rem;
}

int
main(void)
{

  log_init();
  screen_init(140, 40);

  if (!renderer_init(&renderer, &screen)) {
    screen_destroy();
    return 1;
  }

  if (!vt_init(&screen)) {
    renderer_destroy(&renderer);
    screen_destroy();
    return 1;
  }

  vt_render_naive();

  gettimeofday(&start, NULL);

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

    frames++;

    if (frames % 60 == 0) {
      gettimeofday(&end, NULL);
      double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
      char buf[128];
      snprintf(buf, 128, "vt - %ux%u %.2f FPS", screen.cols, screen.rows, frames / elapsed );
      SDL_SetWindowTitle(renderer.window, buf);
    }

    SDL_Delay(12);
  }

  vt_destroy();

  renderer_destroy(&renderer);
  screen_destroy();
  log_destroy();

  VTINFO("Quit successfully!");
  return 0;
}

