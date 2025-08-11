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
#define ISCONTROLC0(c)		(BETWEEN(c, 0, 0x1f) || (c) == 0x7f)
#define ISCONTROLC1(c)		(BETWEEN(c, 0x80, 0x9f))
#define ISCONTROL(c)		(ISCONTROLC0(c) || ISCONTROLC1(c))

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

#define CURSOR_IS_SET(c, flag)    ((c & (flag)) != 0)
#define CURSOR_SET(c, flag)       (c |= (flag))
#define CURSOR_UNSET(c, flag)     (c &= ~(flag))

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

typedef enum {
    ATTR_NONE       = 0,
    ATTR_BOLD       = 1 << 0,   // 00000001
    ATTR_FAINT      = 1 << 1,   // 00000010
    ATTR_ITALIC     = 1 << 2,   // 00000100
    ATTR_UNDERLINE  = 1 << 3,   // 00001000
    ATTR_BLINK      = 1 << 4,   // 00010000
    ATTR_REVERSE    = 1 << 5,   // 00100000
    ATTR_INVISIBLE  = 1 << 6,   // 01000000
    ATTR_STRUCK     = 1 << 7,   // 10000000
} Cursor_Attr;

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

static void vt_cursor_set(const int *attr, int l);

static void vt_insert_blank(u32 n);
static void vt_move_to(u32 x, u32 y);

static void vt_cursor_forward();
static void vt_cursor_backward(u32 n);
static void vt_next_line(void);

static void vt_tty_putc(codepoint_t c) ;

/* Escape Sequences */
static void vt_handle_csi();


/* Render */
static void vt_render_ll(LogicalLine ll);
static void vt_render_naive(void);

static void screen_init(uint16_t cols, uint16_t rows);
static void screen_destroy(void);

/* cmd */
static void vt_cmd_putc(codepoint_t c);
static void vt_cmd_backspace(void);
static void vt_cmd_left(void);
static void vt_cmd_right(void);

/* shell */
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
    .bg = ansi_bg[bg_color],
    .fg = ansi_fg[fg_color],
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

static void
vt_cursor_set(const int *attr, int l)
{
	int i;
	for (i = 0; i < l; i++) {
    VTTRACE("Cursor set %d", attr[i]);
		switch (attr[i]) {
		case 0:
      // vt.cursor.fg &= ~(
      //     ATTR_BOLD       |
      //     ATTR_FAINT      |
      //     ATTR_ITALIC     |
      //     ATTR_UNDERLINE  |
      //     ATTR_BLINK      |
      //     ATTR_REVERSE    |
      //     ATTR_INVISIBLE  |
      //     ATTR_STRUCK     );
			vt.cursor.fg = ansi_fg[WHITE];
			// vt.cursor.bg = ansi_fg[bg_color];
			break;
		case 1:
			vt.cursor.attr |= ATTR_BOLD;
			break;
		case 2:
			vt.cursor.attr |= ATTR_FAINT;
			break;
		case 3:
			vt.cursor.attr |= ATTR_ITALIC;
			break;
		case 4:
			vt.cursor.attr |= ATTR_UNDERLINE;
			break;
		case 5: /* slow blink */
			/* FALLTHROUGH */
		case 6: /* rapid blink */
			vt.cursor.attr |= ATTR_BLINK;
			break;
		case 7:
			vt.cursor.attr |= ATTR_REVERSE;
			break;
		case 8:
			vt.cursor.attr |= ATTR_INVISIBLE;
			break;
		case 9:
			vt.cursor.attr |= ATTR_STRUCK;
			break;
		case 22:
			vt.cursor.fg &= ~(ATTR_BOLD | ATTR_FAINT);
			break;
		case 23:
			vt.cursor.fg &= ~ATTR_ITALIC;
			break;
		case 24:
			vt.cursor.fg &= ~ATTR_UNDERLINE;
			break;
		case 25:
			vt.cursor.fg &= ~ATTR_BLINK;
			break;
		case 27:
			vt.cursor.fg &= ~ATTR_REVERSE;
			break;
		case 28:
			vt.cursor.fg &= ~ATTR_INVISIBLE;
			break;
		case 29:
			vt.cursor.fg &= ~ATTR_STRUCK;
			break;
		case 38:
			// if ((idx = tdefcolor(attr, &i, l)) >= 0)
			// 	vt.cursor.fg = idx;
			break;
		case 39:
			vt.cursor.fg = ansi_fg[fg_color];
			break;
		case 48:
			// if ((idx = tdefcolor(attr, &i, l)) >= 0)
			// 	vt.cursor.bg = idx;
			break;
		case 49:
			vt.cursor.bg = ansi_bg[bg_color];
			break;
		default:
			if (BETWEEN(attr[i], 30, 37)) {
				vt.cursor.fg = ansi_fg[attr[i]-30];
			} else if (BETWEEN(attr[i], 40, 47)) {
				vt.cursor.bg = ansi_fg[attr[i]-30];
			} else if (BETWEEN(attr[i], 90, 97)) {
				vt.cursor.fg = ansi_fg[attr[i] - 90 + 8];
			} else if (BETWEEN(attr[i], 100, 107)) {
				vt.cursor.bg = ansi_fg[attr[i] - 100 + 8];
			} else {
        VTERROR("gfx attr %d unknown\n", attr[i]);
			}
			break;
		}
	}
}

static void
vt_handle_csi()
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
    case 'G': /* CHA -- Move to <col> */
    case '`': /* HPA */
      // DEFAULT(csi_escape_seq.arg[0], 1);
      // vt_move_to(csi_escape_seq.arg[0]-1, vt.cursor.y);
      break;
    case 'H': /* CUP -- Move to <row> <col> */
    case 'f': /* HVP */
      // DEFAULT(csi_escape_seq.arg[0], 1);
      // DEFAULT(csi_escape_seq.arg[1], 1);
      // tmoveato(csi_escape_seq.arg[1]-1, csi_escape_seq.arg[0]-1);
      break;
    case 'I': /* CHT -- Cursor Forward Tabulation <n> tab stops */
      // DEFAULT(csi_escape_seq.arg[0], 1);
      // tputtab(csi_escape_seq.arg[0]);
      break;
    case 'J': /* ED -- Clear screen */
      // switch (csi_escape_seq.arg[0]) {
      //   case 0: /* below */
      //     tclearregion(vt.cursor.x, vt.cursor.y, term.col-1, vt.cursor.y);
      //     if (vt.cursor.y < term.row-1) {
      //       tclearregion(0, vt.cursor.y+1, term.col-1,
      //           term.row-1);
      //     }
      //     break;
      //   case 1: /* above */
      //     if (vt.cursor.y > 1)
      //       tclearregion(0, 0, term.col-1, vt.cursor.y-1);
      //     tclearregion(0, vt.cursor.y, vt.cursor.x, vt.cursor.y);
      //     break;
      //   case 2: /* all */
      //     tclearregion(0, 0, term.col-1, term.row-1);
      //     break;
      //   default:
      //     goto unknown;
      // }
      break;
    case 'K': /* EL -- Clear line */
      // switch (csi_escape_seq.arg[0]) {
      //   case 0: /* right */
      //     tclearregion(vt.cursor.x, vt.cursor.y, term.col-1,
      //         vt.cursor.y);
      //     break;
      //   case 1: /* left */
      //     tclearregion(0, vt.cursor.y, vt.cursor.x, vt.cursor.y);
      //     break;
      //   case 2: /* all */
      //     tclearregion(0, vt.cursor.y, term.col-1, vt.cursor.y);
      //     break;
      // }
      break;
    case 'S': /* SU -- Scroll <n> line up */
      // if (csi_escape_seq.priv) break;
      // DEFAULT(csi_escape_seq.arg[0], 1);
      // tscrollup(term.top, csi_escape_seq.arg[0]);
      break;
    case 'T': /* SD -- Scroll <n> line down */
      // DEFAULT(csi_escape_seq.arg[0], 1);
      // tscrolldown(term.top, csi_escape_seq.arg[0]);
      break;
    case 'L': /* IL -- Insert <n> blank lines */
      // DEFAULT(csi_escape_seq.arg[0], 1);
      // tinsertblankline(csi_escape_seq.arg[0]);
      break;
    case 'l': /* RM -- Reset Mode */
      // tsetmode(csi_escape_seq.priv, 0, csi_escape_seq.arg, csi_escape_seq.narg);
      break;
    case 'M': /* DL -- Delete <n> lines */
      // DEFAULT(csi_escape_seq.arg[0], 1);
      // tdeleteline(csi_escape_seq.arg[0]);
      break;
    case 'X': /* ECH -- Erase <n> char */
      // DEFAULT(csi_escape_seq.arg[0], 1);
      // tclearregion(vt.cursor.x, vt.cursor.y,
      //     vt.cursor.x + csi_escape_seq.arg[0] - 1, vt.cursor.y);
      break;
    case 'P': /* DCH -- Delete <n> char */
      // DEFAULT(csi_escape_seq.arg[0], 1);
      // tdeletechar(csi_escape_seq.arg[0]);
      break;
    case 'Z': /* CBT -- Cursor Backward Tabulation <n> tab stops */
      // DEFAULT(csi_escape_seq.arg[0], 1);
      // tputtab(-csi_escape_seq.arg[0]);
      break;
    case 'd': /* VPA -- Move to <row> */
      // DEFAULT(csi_escape_seq.arg[0], 1);
      // tmoveato(vt.cursor.x, csi_escape_seq.arg[0]-1);
      break;
    case 'h': /* SM -- Set terminal mode */
      // tsetmode(csi_escape_seq.priv, 1, csi_escape_seq.arg, csi_escape_seq.narg);
      break;
    case 'm': 
      VTDEBUG("SGR -- Terminal attribute (color) %d %d", csi_escape_seq.arg[0], csi_escape_seq.arg[1]);
      vt_cursor_set(csi_escape_seq.arg, csi_escape_seq.narg);
      break;
    case 'n': /* DSR -- Device Status Report */
      switch (csi_escape_seq.arg[0]) {
        case 5: /* Status Report "OK" `0n` */
          // ttywrite("\033[0n", sizeof("\033[0n") - 1, 0);
          break;
        case 6: /* Report Cursor Position (CPR) "<row>;<column>R" */
          // len = snprintf(buf, sizeof(buf), "\033[%i;%iR",
          //     vt.cursor.y+1, vt.cursor.x+1);
          // ttywrite(buf, len, 0);
          break;
        // default:
        //   goto unknown;
      }
      break;
    case 'r': /* DECSTBM -- Set Scrolling Region */
      // if (csi_escape_seq.priv) {
      // 	goto unknown;
      // } else {
      // 	DEFAULT(csi_escape_seq.arg[0], 1);
      // 	DEFAULT(csi_escape_seq.arg[1], term.row);
      // 	tsetscroll(csi_escape_seq.arg[0]-1, csi_escape_seq.arg[1]-1);
      // 	tmoveato(0, 0);
      // }
      break;
    case 's': /* DECSC -- Save cursor position (ANSI.SYS) */
      // tcursor(CURSOR_SAVE);
      break;
    case 'u': /* DECRC -- Restore cursor position (ANSI.SYS) */
      // tcursor(CURSOR_LOAD);
      break;
    case ' ':
      // switch (csi_escape_seq.mode[1]) {
      // case 'q': /* DECSCUSR -- Set Cursor Style */
      // 	if (xsetcursor(csi_escape_seq.arg[0]))
      // 		goto unknown;
      // 	break;
      // default:
      // 	goto unknown;
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
    renderer_draw_codepoint(' ', vt.cursor.x, vt.cursor.y, vt.cursor.bg, vt.cursor.fg, vt.cursor.attr);
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

static void
vt_render_ll(LogicalLine ll) {

  char *ptr = vt_scrollback_get(ll.start);
  const char *end = ptr + ll.len + 1;
  VTTRACE("parsing %.*s", end-ptr, ptr);

  ll.has_ansi = true;

  if (!(ll.has_ansi || ll.has_unicode)) {
    for (; ptr < end; ptr++) vt_tty_putc(*ptr);
    return;
  } 

  for (; ptr < end; ptr++) {

    if (*ptr == '\t') {
      for (int i = 0; i < 4; i++)
        vt_cursor_forward();
      continue;
    } else if (*ptr == '\r') {
      for (int i = 0; i < 4; i++)
        vt_move_to(0, vt.cursor.y);
      continue;
    }

    /* --- Control Sequence --- */
    if (*ptr == ESC) { 
      VT_SET(ESC_START);
      VTDEBUG("Escape Sequence Initializer: 0x%x", *ptr);

double_trouble:
      ptr++;
      if (ptr >= end)
        return;

      /* --- CSI Sequence --- */
      if (*ptr == '[') {
        VT_SET(ESC_CSI);

        ptr++;
        if (ptr >= end)
          return;

        char *np;
        long int v;

        csi_escape_seq.narg = 0;
        if (*ptr == '?') { // VT codes
          csi_escape_seq.priv = 1;
          ptr++;
        }

        for (; ptr < end; ptr++) {
          VTTRACE("CSI ARG: %d", *ptr);
          np = NULL;
          v = strtol(ptr, &np, 10);
          VTTRACE("Added argument %ld", v);

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
        } else {
          csi_escape_seq.mode[0] = *ptr;
          csi_escape_seq.mode[1] = (ptr+1 < end) ? *(ptr+1) : '\0';
          VTDEBUG("Parsing CSI with mode %.*s", 2, csi_escape_seq.mode);
          vt_handle_csi();
        }

        VT_UNSET(ESC_CSI);

        if (*ptr == ESC) {
          goto double_trouble;
        } 

        VT_UNSET(ESC_START);

        if (ptr == end)
          return;

      }

      /* --- OSC Sequence --- */
      else if (*ptr == ']') {
        VT_SET(ESC_CSI);
        ptr++;

        VTDEBUG("Parsing OSC...");

        for (; ptr < end; ptr++) {
          if (*ptr == '0') {
            // SDL_SetWindowTitle();
          }

          if (*ptr == BEL) {
            break;
          }
        }

        VT_UNSET(ESC_CSI);
        VT_UNSET(ESC_START);
        if (ptr == end)
          return;
      } 
      /* --- Not a Valid Control Sequence --- */
      else {
        VTWARN("Unknown escape sequence initializer 0x%x", *ptr);

        /* --- UTF8 --- */
        // if (utf8)
        // TODO: parse utf8 here
        // do nothing for now
      }
    }
    /* --- Everything else --- */
    else {
      /* --- Visible ASCII --- */
      if (*ptr >= 32) { 
        vt_tty_putc(*ptr);
      }
    }

  } /* --- End of For Loop --- */
}

static void
vt_render_naive(void) 
{

  vt_update_logical_lines();

  Screen *screen = vt.screen;
  LogicalLine *consume = vt_ll_get();

  Terminal_Cursor last_cursor = vt.cursor;
  while ((consume=vt_ll_get())) {
    vt_render_ll(*consume);
    last_cursor = vt.cursor;
    vt.state = 0;

    vt.cursor.x = 0;
    if (vt.cursor.y < screen->rows) {
      vt.cursor.y++;
    } else {
      renderer_insert_newline();
    }

  }

  vt.cursor = last_cursor;

}

static void
vt_cursor_forward() 
{
  vt.cursor.x++;
  if (vt.cursor.x == screen.cols) {
    vt.cursor.x = 0;
    if (vt.cursor.y == screen.rows - 1) {
      renderer_insert_newline();
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
  renderer_draw_codepoint(c, vt.cursor.x, vt.cursor.y, vt.cursor.fg, vt.cursor.bg, vt.cursor.attr);
  vt_cursor_forward(&vt.cursor);
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

  renderer_draw_codepoint(' ', vt.cursor.x, vt.cursor.y, vt.cursor.fg, vt.cursor.y, vt.cursor.attr);
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
  vt_cursor_forward(&vt.cursor);
}

static size_t
vt_sh_read() 
{

  struct pollfd fds[1];
  fds[0].fd = vt.sh_fd;
  fds[0].events = POLLIN;

  CBuffer *q = &vt.scrollback;

  int ret = poll(fds, 1, 500); 
  u64 r = 0;
  if (ret > 0 && (fds[0].revents & POLLIN)) {
    char data[16000];
    // r = read(vt.sh_fd, q->buffer+(q->write%q->buffer_size), q->buffer_size);
    r = read(vt.sh_fd, data, 16000);
    if (r > 0) {
      vt_scrollback_push(data, r);
      q->write += r;
      VTDEBUG("vt_read: recieved %ld bytes", r);
    } else if (r == 0) {
      VTWARN("Shell exited");
    }
  }

  return r;
}


static size_t
vt_sh_write(const char * const src, size_t len) 
{
  VTDEBUG("Writing %.*s to the shell", len, src);
  ssize_t r = write(vt.sh_fd, src, len);
  if (r < 0) { 
    // ttyputs("Failed to write to the shell");
    VTFATAL("Failed to write to the shell");
  }

  vt_sh_read();
  vt_scrollback_push("\n", 1);
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

    if (ll.has_ansi == false && ISCONTROL(*ll_end) ) {
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
vt_ll_get() 
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

    SDL_WaitEventTimeout(NULL, -1);
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

