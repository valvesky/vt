#define _GNU_SOURCE // has to be at the top                                                                                                                                             
/*
 *                  _/
 *   _/      _/  _/_/_/_/
 *  _/      _/    _/
 *   _/  _/      _/
 *    _/          _/_/ 
 *
 */

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
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <pty.h>
#include <limits.h>
#include <sys/time.h>
#include <poll.h>

/* debugging is good for you */
#ifdef DEBUG
#define LOG_WARN_ENABLED 0
#define LOG_INFO_ENABLED 0
#define LOG_DEBUG_ENABLED 0
#define LOG_TRACE_ENABLED 1
#else
#define LOG_WARN_ENABLED  0
#define LOG_INFO_ENABLED  0
#define LOG_DEBUG_ENABLED 0
#define LOG_TRACE_ENABLED 0
#endif

#include "vt_circ_buf.c"

#ifdef _VT_OPENGL 
#include "opengl/glad-3.3.c"
#include <SDL3/SDL_opengl.h>
#include "vt_renderer_opengl_naive.c"
#else
#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>
#include "vt_renderer_vulkan.c"
#endif

#include "vt.h"
#include "config.h"
#include "vt_debug.h"

#define BEL '\x07'
#define ESC '\x1B'

/* Arbitrary sizes */
#define UTF_INVALID   0xFFFD
#define UTF_SIZ       4
#define ESC_ARG_SIZ   16
#define STR_ARG_SIZ   ESC_ARG_SIZ
#define CMD_SIZ       1024

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

enum term_mode {
	MODE_WRAP        = 1 << 0,
	MODE_INSERT      = 1 << 1,
	MODE_ALTSCREEN   = 1 << 2,
	MODE_CRLF        = 1 << 3,
	MODE_ECHO        = 1 << 4,
	MODE_PRINT       = 1 << 5,
	MODE_UTF8        = 1 << 6,
};

enum cursor_movement {
	CURSOR_SAVE,
	CURSOR_LOAD
};

enum cursor_state {
	CURSOR_DEFAULT  = 0,
	CURSOR_WRAPNEXT = 1,
	CURSOR_ORIGIN   = 2
};


typedef struct {
  char priv;
  int arg[ESC_ARG_SIZ];
  int narg; /* number of args */
  char mode[2];
} CSIEscape;

/* --- Variables --- */
static Terminal vt = {0};
static CSIEscape csi_escape_seq = {0};

static Screen screen = {0};
static Renderer renderer = {0};
static struct timeval start, end;
static bool running = true;
static uint64_t frames = 0;

static char cmd_buf[CMD_SIZ] = {0};

/* --- Function Declarations --- */
extern bool vt_init(Screen*);
extern void vt_destroy(void);

static void vt_insert_blank(u32 n);
static void vt_move_to(u32 x, u32 y);

static void vt_cursor_forward(void);
static void vt_cursor_backward(u32 n);
static void vt_next_line(void);

static void vt_tty_putc(codepoint_t c) ;

/* Escape Sequences */
static u32  vt_parse_csi(const char *ptr, const char * const end);
static void vt_handle_csi(void);

static u32  vt_parse_osc(const char *ptr, const char * const end);

/* Render */
static void vt_render_ll(LogicalLine ll, bool active_line);
static void vt_render_naive(void);

static void screen_init(uint16_t cols, uint16_t rows);
static void screen_destroy(void);

/* cmd */
static void vt_cmd_putc(codepoint_t c);
static void vt_cmd_backspace(void);
static void vt_cmd_left(void);
static void vt_cmd_right(void);

/* shell */
static void crash(const char* err);

static size_t vt_sh_read();
static size_t vt_sh_write(const char * const src, size_t len);

static void vt_scrollback_push(char *str, size_t len);
static char* vt_scrollback_get(uint16_t idx);

static void vt_update_logical_lines(void);
static LogicalLine* vt_ll_get(void);

static LogicalLine vt_ll_get_nonterminated(void); 
static LogicalLine vt_ll_get_last(size_t i);

/* --- Function Definitions --- */

bool
vt_init(Screen *screen) 
{
  /* --- buffers --- */
  cbuffer_init(&vt.scrollback, getpagesize()*3);
  cbuffer_init(&vt.logical_lines, getpagesize());

  /* --- shell --- */
  int master, slave;
  if (openpty(&master, &slave, NULL, NULL, NULL) < 0) {
    VTFATAL("Could not open tty.");
    return false;
  }
    
  vt.sh_pid = fork();
  if (vt.sh_pid < 0) {
    VTFATAL("Could not open tty.");
    return false;
  }

  if (vt.sh_pid == 0) {
    close(master);
    setsid(); /* create a new process group */

    dup2(slave, STDIN_FILENO);
    dup2(slave, STDOUT_FILENO);
    dup2(slave, STDERR_FILENO);

    if (ioctl(slave, TIOCSCTTY, NULL) < 0) {
      VTFATAL("ioctl failed! ");
      return false;
    }
    if (slave > STDERR_FILENO) {
      close(slave);
    }

    setenv("TERM", "xterm-256color", 1);
    execlp("bash", "bash", "--login", NULL);
    _Exit(EXIT_SUCCESS);
  }

  vt.sh_fd = master;

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

void
vt_destroy() 
{
  if (vt.sh_fd > 0) close(vt.sh_fd); 
  if (vt.sh_pid > 0) waitpid(vt.sh_pid, NULL, 0);
  VTINFO("[Shell %-d] Exited successfully", vt.sh_pid);

  cbuffer_destroy(&vt.scrollback);
  cbuffer_destroy(&vt.logical_lines);
  memset(&vt, 0, sizeof vt);
}

static u32 
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
  // csi_escape_seq.ll_end = end;
  vt_handle_csi();

  VT_UNSET(ESC_CSI);
  return ptr - start;
}

static void
vt_handle_csi(void)
{
  assert(VT_IS_SET(ESC_CSI));

  switch (csi_escape_seq.mode[0]) {
    default:
      VTWARN("CSI MODE INVALID OR NOT SUPPORTED: %c", csi_escape_seq.mode[0]);
      csi_escape_seq = (CSIEscape) {0};
      break;
    case '@': 
      VTTRACE("ICH -- Insert %d blank char", csi_escape_seq.arg[0]);
      DEFAULT(csi_escape_seq.arg[0], 1);
      vt_insert_blank(csi_escape_seq.arg[0]);
      break;
    case 'A':
      VTTRACE("CUU -- Cursor %d Up");
      DEFAULT(csi_escape_seq.arg[0], 1);
      vt_move_to(vt.cursor.x, vt.cursor.y-csi_escape_seq.arg[0]);
      break;
    case 'B': /* CUD */
    case 'e': /* VPR */
      VTTRACE("CUD/VPR -- Cursor %d Down", csi_escape_seq.arg[0]);
      DEFAULT(csi_escape_seq.arg[0], 1);
      vt_move_to(vt.cursor.x, vt.cursor.y+csi_escape_seq.arg[0]);
      break;
      /* TODO? */
      /* case 'i': Media Copy 
       break; */
    case 'c': /* DA -- Device Attributes */
      /* TODO? */
      VTTRACE("DA -- Device Attributes");
      // if (csi_escape_seq.arg[0] == 0)
        // vt_scrollback_push(char *str, size_t len)
        // ttywrite(vtiden, strlen(vtiden), 0);
      break;
    case 'b': /* REP -- if last char is printable print it <n> more times */
      VTTRACE("REP -- Cursor %d Forward", csi_escape_seq.arg[0]);
      DEFAULT(csi_escape_seq.arg[0], 1);
      /* TODO */
      // if (term.lastc)
      //   while (csi_escape_seq.arg[0]-- > 0)
      //     tputc(term.lastc);
      break;
    case 'C': /* CUF -- Cursor <n> Forward */
    case 'a': 
      VTTRACE("CUF/HPR -- Cursor %d Forward", csi_escape_seq.arg[0]);
      DEFAULT(csi_escape_seq.arg[0], 1);
      vt_move_to(vt.cursor.x+csi_escape_seq.arg[0], vt.cursor.y);
      break;
      VTTRACE("CUB -- Cursor %d Backward", csi_escape_seq.arg[0]);
      DEFAULT(csi_escape_seq.arg[0], 1);
      vt_move_to(vt.cursor.x-csi_escape_seq.arg[0], vt.cursor.y);
      break;
    case 'E': 
      VTTRACE("CNL -- Cursor %d Down and first col", csi_escape_seq.arg[0]);
      DEFAULT(csi_escape_seq.arg[0], 1);
      vt_move_to(0, vt.cursor.y+csi_escape_seq.arg[0]);
      break;
    case 'F': 
      VTTRACE("CPL -- Cursor %d Up and first col", csi_escape_seq.arg[0]);
      DEFAULT(csi_escape_seq.arg[0], 1);
      vt_move_to(0, vt.cursor.y-csi_escape_seq.arg[0]);
      break;
      
    case 'g': 
      VTTRACE("TBC -- Tabulation clear");
      /* TODO */
      // switch (csi_escape_seq.arg[0]) {
      //   case 0: /* clear current tab stop */
      //     term.tabs[vt.cursor.x] = 0;
      //     break;
      //   case 3: /* clear all the tabs */
      //     memset(term.tabs, 0, term.col * sizeof(*term.tabs));
      //     break;
      //   default:
      //     goto unknown;
      // }
      break;
  }
}

static void
vt_insert_blank(u32 n) 
{
  if (n == 0) return;
  assert(VT_IS_SET(ESC_CSI));

  /* VT100/xterm behavior:
   * - Characters at the right edge are pushed out and lost.
   * - The line stays the same width.
   * - Characters beyond the edge are discarded, not wrapped. */

  /*     |--- n ---|--- n ---|
   * | | | | | | | | | | | | | | | | | | |
   *     ^left      ^right   ^ end  */

  for (; n > 0; --n) {
    renderer_draw_codepoint(' ', vt.cursor.x, vt.cursor.y, vt.cursor.bg, vt.cursor.fg);
    // renderer_copy(left, right);
    vt_cursor_forward();
  }
}

static void
vt_move_to(u32 x, u32 y)
{
  vt.cursor.x = MIN(x, screen.cols-1);
  vt.cursor.y = MIN(y, screen.rows-1);
}


static u32
vt_parse_osc(const char *ptr, const char * const end) {
  
  assert(VT_IS_SET(ESC_START) && VT_IS_SET(ESC_CSI));
  VTDEBUG("Parsing OSC...");

  if (*ptr == ']') ptr++; // just in case

  const char *start = ptr;
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
vt_render_ll(LogicalLine ll, bool active_line) {

  Terminal_Cursor save = vt.cursor;

  char *ptr = vt_scrollback_get(ll.start);
  char *end = ptr + ll.len;
  VTTRACE("parsing %.*s", end-ptr, ptr);

  if (!(ll.has_ansi || ll.has_unicode)) {
    for (; ptr < end; ptr++) vt_tty_putc(*ptr);
    return;
  } else {
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
          vt_tty_putc(*ptr);
        }
      } else { // in escape sequence
        if (*ptr == '[') {
          VT_SET(ESC_CSI);
          ptr++;
          ptr += vt_parse_csi(ptr, end);
          VT_UNSET(ESC_START);
        } else if (*ptr == ']') {
          VT_SET(ESC_CSI);
          ptr++;
          ptr += vt_parse_osc(ptr, end);
          VT_UNSET(ESC_START);
        } else {
          VTWARN("Unknown escape sequence initializer %c", *ptr);
        }
      }
    }
  }

  if (active_line)
    vt.cursor = save;
}

static void
vt_render_naive(u32 line) 
{
  if ()
  Screen *screen = vt.screen;
  LogicalLine *consume = NULL;

  while ((consume = vt_ll_get())) {
    vt_render_ll(*consume, false);
    vt.state = 0;
    if (vt.cursor.y < screen->rows) {
      vt.cursor.y++;
      vt.cursor.x = 0;
    }
  } 

  LogicalLine cmd = vt_ll_get_nonterminated();
  vt_render_ll(cmd, true);
}

static void
vt_cursor_forward(void) 
{
  vt.cursor.x++;
  if (vt.cursor.x == screen.cols) {
    vt.cursor.x = 0;
    if (vt.cursor.y == screen.rows - 1) {
      VTTRACE("new line");
      // renderer_newline();
    } else {
      vt.cursor.y++;
    }
  }
}

static void
vt_cursor_backward(u32 n) 
{
  for (; n > 0; n--) {
    if (vt.cursor.x == 0) {
      vt.cursor.x = screen.cols - 1;
      if (vt.cursor.y > 0) vt.cursor.y--;
    } else {
      vt.cursor.x--;
    };
  }
}

static void
vt_next_line(void) {
  vt_move_to(0, vt.cursor.y+1);
}

static void
vt_tty_putc(codepoint_t c) 
{
  renderer_draw_codepoint(c, vt.cursor.x, vt.cursor.y, vt.cursor.fg, vt.cursor.bg);
  vt_cursor_forward();
}

static void
vt_cmd_putc(codepoint_t c) 
{
  switch (c) {
    default:
      if (vt.cmd_pos < CMD_SIZ) {
        vt_tty_putc(c);
        cmd_buf[vt.cmd_pos++] = c;
      }
      break;
    case '\r':
    case '\n': 
      {
        cmd_buf[vt.cmd_pos++] = '\n';
        vt_sh_write(cmd_buf, vt.cmd_pos);
        vt_next_line();
        vt.cmd_pos = 0;
      }
      break;
    case '\t':
      vt_insert_blank(4);
  }
}

static void
vt_cmd_backspace(void)
{
  VTTRACE("cmd backspace %d\n", vt.cmd_pos);
  if (vt.cmd_pos > 0) {
    vt_cursor_backward(1);
    vt.cmd_pos--;
  }

  renderer_draw_codepoint(' ', vt.cursor.x, vt.cursor.y, vt.cursor.fg, vt.cursor.y);
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
  vt_cursor_forward();
}

static void
crash(const char* err)
{
  VTFATAL("%s", err);
  _Exit(EXIT_FAILURE);
}

static size_t
vt_sh_read() 
{

  struct pollfd fds[1];
  fds[0].fd = vt.shell.fd;
  fds[0].events = POLLIN;

  int ret = poll(fds, 1, 500); 
  CBuffer *q = &vt.scrollback;

  u64 r;
  if (ret > 0 && (fds[0].revents & POLLIN)) {
    r = read(vt.shell.fd, q->buffer+(q->write%q->buffer_size), q->buffer_size);
    if (r > 0) {
      q->write += r;
      VTDEBUG("vt_read: recieved %ld bytes", r);
    } else if (r == 0) {
      vt_scrollback_push("Process exited.\n", 17);
      vt_update_logical_lines();
      vt_render_naive();
      VTWARN("Shell exited");
    }
  }

  return r;
}


static size_t
vt_sh_write(const char * const src, size_t len) 
{
  VTDEBUG("Writing %.*s to the shell", len, src);
  ssize_t r = write(vt.shell.fd, src, len);
  if (r < 0) { 
    ttyputs("Failed to write to the shell");
    VTFATAL("Failed to write to the shell");
  }

  vt_sh_read();
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
  char *ll_end = ll_beg+1;

  LogicalLine ll = {0, 0, 0, false, false };
  while (sc->read < sc->write-1) {

    if (ll.has_ansi == false && *ll_end == ESC) {
      ll.has_ansi = true;
    }

    if (ll.has_unicode == false && ((u8) *ll_end) > 127 ) {
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


int
main(void)
{

  log_init();

  if (!renderer_init(&screen)) {
    screen_destroy();
    return 1;
  }

  if (!vt_init(&screen)) {
    renderer_destroy();
    screen_destroy();
    return 1;
  }

  gettimeofday(&start, NULL);

  SDL_Event event;
  SDL_StartTextInput(context.window);

  while (running) {

    SDL_WaitEventTimeout(NULL, 1000);
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
          switch (event.key.key) {
            case SDLK_RETURN:
              vt_cmd_putc('\r');
              break;
            case SDLK_BACKSPACE:
                vt_cmd_backspace();
              break;
            case SDLK_LEFT:
              vt_cmd_left();
              break;
            case SDLK_RIGHT:
              vt_cmd_right();
              break;
          }
          break;
        case SDL_EVENT_TEXT_INPUT:
          vt_cmd_putc(event.text.text[0]);
          // vt_tty_putc(event.text.text[0]);
          break;
        case SDL_EVENT_WINDOW_RESIZED:
          {
          u32 index = vt.cursor.y * screen.cols + vt.cursor.x;
          renderer_resize(event.display.data1, event.display.data2);
          vt.cursor.x = index % screen.cols;
          vt.cursor.y = index / screen.cols;
          }
          break;
        case SDL_EVENT_QUIT:
          running = false;
      }
    }

    // renderer_clear();
    platform_clear_window(ansi_bg[bg_color]);
    vt_render_naive();
    renderer_sync();
    platform_swap_window();
    frames++;

    if (frames % 60 == 0) {
      gettimeofday(&end, NULL);
      double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
      char buf[128];
      snprintf(buf, 128, "vt - %ux%u %.2f FPS", screen.cols, screen.rows, frames / elapsed );
      SDL_SetWindowTitle(context.window, buf);
    }

    // SDL_Delay(12);
  }

  VTDEBUG("Frames: %ld\n", frames);
  vt_destroy();

  renderer_destroy();
  log_destroy();

  VTINFO("Quit successfully!");
  return 0;
}

