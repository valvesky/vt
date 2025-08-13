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

#include <immintrin.h>
#include <emmintrin.h>

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

#define SCREEN_GROWTH_FACTOR 2

/* Flags */
#define MODE_IS_SET(flag)    ((vt.mode & (flag)) != 0)
#define MODE_SET(flag)       (vt.mode |= (flag))
#define MODE_UNSET(flag)     (vt.mode &= ~(flag))

#define VT_IS_SET(flag)    ((vt.state & (flag)) != 0)
#define VT_SET(flag)       (vt.state |= (flag))
#define VT_UNSET(flag)     (vt.state &= ~(flag))

#define LL_COUNT ((vt.logical_lines.write - vt.logical_lines.read) / sizeof(Line))

/* --- Globals --- */
static Terminal vt = {0};

static struct timeval start, end;
static bool running = true;
static uint64_t frames = 0;
static char cmd_buf[CMD_SIZ] = {0};

/* --- Function Declarations --- */
static Screen screen_init(uint16_t cols, uint16_t rows);
static void screen_destroy(Screen *s);
static bool screen_resize(Screen *s, uint32_t cols, uint32_t rows);

static void vt_next_line();
static void vt_cursor_forward();
static void vt_cursor_backward(u32 n);

static bool vt_init(u32 cols, u32 rows); /* Initializes terminal resources */
static void vt_destroy(); /* Free terminal resources */
static void vt_resize(u32 cols, u32 rows);

static void vt_scrollback_push(char *str, size_t len); /* Push to circular buffer */
static char* vt_scrollback_get(Line ll); /* Get pointer from line buffer */

static Line* vt_line_get(void); /* Consume logical line */
static Line vt_line_get_last(size_t i); /* Get last i-ultimate logical line (does not consume) */

static void vt_handle_C0(char code); /* handles the original 7-bit ANSI codes */
static void vt_handle_C1(unsigned char code); /* handles the 8-bit ECMA codes */
static bool vt_handle_esc(unsigned char ascii); /* handles escape codes */
static void vt_handle_str(); /* handles escape codes */
static bool vt_parse_csi(); /* Returns true if sequence is valid */
static void vt_handle_csi();

static void vt_char_feed(char *ptr, bool control_codes, bool high_bit); /* feed char to the state machine */
static void vt_line_feed(Line line); /* consumes line buffer */
static void vt_parse_input(void); /* consumes scrollback buffer */

static void vt_cursor_set(const int *attr, int l);

static void vt_insert_blank(u32 n);
static void vt_move_to(u32 x, u32 y);
static void vt_move_to_absolute(u32 x, u32 y);

/* cmd */
static void vt_cmd_putc(codepoint_t c);
static void vt_cmd_backspace(void);
static void vt_cmd_left(void);
static void vt_cmd_right(void);

/* shell */
static size_t vt_sh_read();
static size_t vt_sh_write(const char * const src, size_t len);

/* --- Function Definitions --- */

static Screen
screen_init(uint16_t cols, uint16_t rows)
{
  Screen new = {0};
  new.cell_buffer = calloc(rows*cols, sizeof *new.cell_buffer);
  new.cols = cols;
  new.rows = rows;
  new.capacity = cols*rows;
  return new;
}

static void
screen_destroy(Screen *s)
{
  if (s->cell_buffer)
    free(s->cell_buffer);
  memset(s, 0, sizeof *s);
}

static bool
screen_resize(Screen *s, uint32_t cols, uint32_t rows) 
{
  if (cols*rows <= s->capacity) {
    VTDEBUG("Screen has enough capacity for = %ux%u", cols, rows);
    s->cols = cols;
    s->rows = rows;
    return true;
  }

  size_t old_cap = s->capacity;
  size_t new_cap = old_cap * SCREEN_GROWTH_FACTOR;

  size_t type = sizeof(*s->cell_buffer);
  Terminal_Cell *new = realloc(s->cell_buffer, new_cap * type);
  if (new) {
    s->cell_buffer = new;
    s->cols = cols;
    s->rows = rows;
    s->capacity = new_cap;
    memset(s->cell_buffer + old_cap, 0, (new_cap-old_cap) * sizeof *s->cell_buffer);
    VTDEBUG("Resize Screen (realloc) = %ux%u", cols, rows);
    return true;
  } else {
    new = calloc(new_cap, sizeof *s->cell_buffer);
    memcpy(new, s->cell_buffer, old_cap * sizeof *s->cell_buffer);
    free(s->cell_buffer);
    s->cell_buffer = new;
    s->cols = cols;
    s->rows = rows;
    s->capacity = new_cap;
    VTDEBUG("Resize Screen (calloc) = %ux%u", cols, rows);
    return true;
  }

  VTWARN("Failed to resize screen!");
  return false;
}

static void
vt_next_line() {

  Screen *s = MODE_IS_SET(MODE_ALTSCREEN) ? &vt.alt : &vt.screen;

  vt.bot++;

  vt.cursor.x = 0;
  
  if (vt.cursor.y == s->rows - 1) {
    memmove(s->cell_buffer, s->cell_buffer+s->cols, s->cols*(s->rows-1) * sizeof *s->cell_buffer);
    memset(s->cell_buffer+s->cols*(s->rows-1), 0, s->cols * sizeof *s->cell_buffer); /* dont forget to zero the last line */
  } else {
    vt.cursor.y = vt.cursor.y+1;
  }
}

static void
vt_cursor_forward() 
{
  vt.cursor.x++;
  if (vt.cursor.x == vt.screen.cols) {
    vt_next_line();
  }
}

static void
vt_cursor_backward(u32 n) 
{
  for (; n > 0; n--) {
    if (vt.cursor.x == 0) {
      vt.cursor.x = vt.screen.cols - 1;
      if (vt.cursor.y > 0) vt.cursor.y--;
    } else {
      vt.cursor.x--;
    };
  }
}

static void
vt_screen_putc(codepoint_t c) 
{
  u32 idx = vt.cursor.x + vt.cursor.y*vt.screen.cols;
  assert(idx < vt.screen.capacity);

  vt.screen.cell_buffer[idx].codepoint = c;
  vt.screen.cell_buffer[idx].bg = vt.cursor.bg << 8;
  vt.screen.cell_buffer[idx].fg = (vt.cursor.fg << 8) | vt.cursor.attr;
  vt_cursor_forward(&vt.cursor);
}

static bool
vt_init(u32 cols, u32 rows) 
{
  /* --- buffers --- */
  cbuffer_init(&vt.scrollback, getpagesize()*3);
  cbuffer_init(&vt.logical_lines, getpagesize());

  vt.screen = screen_init(cols, rows); // pretty standard terminal size
  vt.alt = screen_init(cols, rows);
  vt.top = 0;
  vt.bot = 0;

  vt.cursor = (Terminal_Cursor) {
    .bg = ansi_bg[bg_color],
    .fg = ansi_fg[fg_color],
    .x = 0, .y = 0,
  };

  vt.mode = 0;
  vt.state = 0;

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
      _Exit(1);
    }
    if (slave > STDERR_FILENO) {
      close(slave);
    }

    setenv("TERM", "xterm-256color", 1);
    execlp("bash", "bash", "--login", NULL);
    _Exit(1);
  }

  vt.sh_fd = master;

  vt_sh_read();
  return true;
}

static void
vt_destroy() 
{
  if (vt.sh_fd > 0) close(vt.sh_fd); 
  if (vt.sh_pid > 0) waitpid(vt.sh_pid, NULL, 0);
  VTINFO("[Shell %-d] Exited successfully", vt.sh_pid);

  screen_destroy(&vt.screen);
  screen_destroy(&vt.alt);
  cbuffer_destroy(&vt.scrollback);
  cbuffer_destroy(&vt.logical_lines);
  memset(&vt, 0, sizeof vt);
}

static void
vt_resize(u32 cols, u32 rows)
{
  screen_resize(&vt.screen, cols, rows);
  screen_resize(&vt.alt, cols, rows);
}

static void
vt_scrollback_push(char *str, size_t len) 
{
  cbuffer_push_overwrite(&vt.scrollback, str, len);
}

static  char *
vt_scrollback_get(Line l) 
{
  return &vt.scrollback.buffer[l.start % vt.scrollback.buffer_size];
}

static Line*
vt_line_get() 
{
  return (Line*) cbuffer_read(&vt.logical_lines, sizeof(Line) );
}

/* get last N-ultimate logical line */
static Line
vt_line_get_last(size_t i)
{
  assert(i <= LL_COUNT);

  CBuffer *cb = &vt.logical_lines;
  char *write_virtual = cb->buffer+(cb->write%cb->buffer_size) + cb->buffer_size - (i)*sizeof(Line); 
  return *((Line*)write_virtual);
}

static void
vt_handle_C0(char code)
{
  /* VT500 codes from https://vt100.net/docs/vt510-rm/chapter4.html */
  switch (code) {
    case BEL: /* beep */
      if (vt.state & ESC_STR_END) {
        vt_handle_str();
      } 
      break;
    case BS: 
      /* BS --	Moves the cursor one character position to the left.
       * If the cursor is at the left margin, no action occurs. */
      if (vt.cursor.x > 0) {
        vt.cursor.x--;
      }
      break;
    case HT: 
      /* HT -- Moves the cursor to the next tab stop. If there are no more
       * tab stops, the cursor moves to the right margin.
       * HT does not cause text to auto wrap. */

      break;
    case LF: 
      /* LF --  Causes a line feed or a new line operation,
       * depending on the setting of line feed/new line mode. */
      return;

      /* Vertical tab 	VT -- 0/11 	Treated as LF. */
    case VT:
      break;
      /* Form feed 	FF -- 0/12 	Treated as LF. */
    case FF:
      break;
      /* Carriage return 	CR -- 0/13 	Moves the cursor to the left margin on 
       * the current line. */
    case CR:
      break;
      /* Shift out (locking shift 1) 	SO (LS1) -- 0/14 	Maps the G1 character
       * set into GL. You designate G1 by using a select character set (SCS) sequence. */
    case S1:
      break;
      /* Shift in (locking shift 0) 	SI (LS0) -- 0/15 	Maps the G0 character set
       * into GL. You designate G0 by using a select character set (SCS) sequence. */
    case S0:
      break;
      /* Cancel 	CAN -- 1/8 	Immediately cancels an escape sequence, control sequence, or device control string in progress. In this case, the VT510 does not display any error character. */
    case CAN:
      break;
      /* Substitute 	SUB -- 1/10 	Immediately cancels an escape sequence, control sequence, or device control string in progress, and displays a reverse question mark as an error character. */
    case SUB:
      break;
      /* Escape 	ESC -- 1/11 	Introduces an escape sequence. ESC also cancels any escape sequence, control sequence, or device control string in progress. */
    case ESC:
      break;
      /* Delete 	DEL -- 7/15 	Ignored when received, unless a 96- character set is mapped into GL. DEL is not used as a fill character. Digital does not recommend using DEL as a fill character. Use NUL instead. */
    case DEL:
      break;

  }







  VTTRACE("Handle C0: 0x%02X", code);
  // int i;
  switch (code) {
    case '\t':   /* HT */
      // tputtab(1);
      return;
    case '\b':   /* BS */
      return;
    case '\r':   /* CR */
      vt_move_to(0, vt.cursor.y);
      return;
    case '\f':   /* LF */
    case '\v':   /* VT */
    case '\n':   /* LF */
      /* go to first col if the mode is set */
      VTDEBUG("GO TO NEXT LINE");
      vt_next_line(&vt.screen);
      return;
    case '\a':   /* BEL */
    case '\x1B': /* ESC */
      vt.state &= ~(ESC_CSI|ESC_ALTCHARSET|ESC_TEST);
      vt.state |= ESC_START;
      return;
    case '\016': /* SO (LS1 -- Locking shift 1) */
    case '\017': /* SI (LS0 -- Locking shift 0) */
      // vt.cursorharset = 1 - (ascii - '\016');
      return;
    case '\032': /* SUB */
      // tsetchar('?', &vt.cursor.attr, vt.cursor.x, vt.cursor.y);
      /* FALLTHROUGH */
    case '\030': /* CAN */
      // csireset();
      
      break;
  }
  /* only CAN, SUB, \a and C1 chars interrupt a sequence */
  vt.state &= ~(ESC_STR_END|ESC_STR);
}

static void
vt_handle_C1(unsigned char code)
{
  switch (code) {
    case 0x88:   /* HTS -- Horizontal tab stop */
      // vt.tabs[vt.cursor.x] = 1;
      break;
    case 0x9a:   /* DECID -- Identify Terminal */
      // ttywrite(vtiden, strlen(vtiden), 0);
      break;
    case 0x9f:   /* APC -- Application Program Command */
      // tstrsequence(ascii);
      return;
  }

  vt.state &= ~(ESC_STR_END|ESC_STR);
}

static bool
vt_handle_esc(unsigned char ascii)
{
  VTTRACE(" H A N D L E  E S C ");
	switch (ascii) {
	case '#':
		vt.state |= ESC_TEST;
		return 0;
	case '%':
		vt.state |= ESC_UTF8;
		return 0;
	case 'P': 
    // VTTRACE("DCS -- Device Control String");
	case '_': 
    // VTTRACE("APC -- Application Program Command");
	case '^': 
    // VTTRACE("PM -- Privacy Message");
	case ']': 
    // VTTRACE("OSC -- Operating System Command");
	case 'k': 
    // VTTRACE("old title set compatibility");
		// tstrsequence(ascii);
		return 0;
	case 'n': 
    // VTTRACE("LS2 -- Locking shift 2");
	case 'o': 
    // VTTRACE("LS3 -- Locking shift 3");
		// vt.charset = 2 + (ascii - 'n');
		break;
	case '(': 
    // VTTRACE("GZD4 -- set primary charset G0");
	case ')': 
    // VTTRACE("G1D4 -- set secondary charset G1");
	case '*': 
    // VTTRACE("G2D4 -- set tertiary charset G2");
	case '+': 
    VTTRACE("G3D4 -- set quaternary charset G3");
		// vt.icharset = ascii - '(';
		// vt.state |= ESC_ALTCHARSET;
		return 0;
	case 'D': 
    VTTRACE("IND -- Linefeed");
		if (vt.cursor.y == vt.bot) {
			// tscrollup(vt.top, 1);
		} else {
			// tmoveto(vt.cursor.x, vt.cursor.y+1);
		}
		break;
	case 'E': 
    VTTRACE("NEL -- Next line");
		// tnewline(1); /* always go to first col */
		break;
	case 'H': 
    VTTRACE("HTS -- Horizontal tab stop");
		// vt.tabs[vt.cursor.x] = 1;
		break;
	case 'M': 
    VTTRACE("RI -- Reverse index");
		if (vt.cursor.y == vt.top) {
			// tscrolldown(vt.top, 1);
		} else {
      vt_move_to(vt.cursor.x, vt.cursor.y-1);
		}
		break;
	case 'Z': 
    VTTRACE("DECID -- Identify Terminal");
		// ttywrite(vtiden, strlen(vtiden), 0);
		break;
	case 'c': 
    VTTRACE("RIS -- Reset to initial state");
		// treset();
		// resettitle();
		// xloadcols();
		// xsetmode(0, MODE_HIDE);
		break;
	case '=': 
    VTTRACE("DECPAM -- Application keypad");
		// xsetmode(1, MODE_APPKEYPAD);
		break;
	case '>': 
    VTTRACE("DECPNM -- Normal keypad");
		// xsetmode(0, MODE_APPKEYPAD);
		break;
	case '7': 
    VTTRACE("DECSC -- Save Cursor");
		// tcursor(CURSOR_SAVE);
		break;
	case '8': 
    VTTRACE("DECRC -- Restore Cursor");
		// tcursor(CURSOR_LOAD);
		break;
	case '\\': 
    VTTRACE("ST -- String Terminator");
		if (vt.state & ESC_STR_END) {
      vt_handle_str();
    }
		break;
	default:
    VTWARN("Unknown escape sequence 0x%02X", ascii);
    break;
	}

  return 1;
}

static void
vt_char_feed(char *ptr, bool control_codes, bool high_bit)
{
  /*
   * NOTE: there is probably a lot still that can be optimized here
   * given the information supplied by the preprocessor 
   */

  unsigned char ch = *ptr;
  VTTRACE("Char Feed 0x%02X", ch);

  assert(control_codes || high_bit || VT_IS_SET(ESC_START) ); // only call this function if needed

  // char codepoint[UTF_SIZ];
  bool control = false;
  int len = 0;

  control = ISCONTROL((int) ch);

  // if (high_bit || MODE_IS_SET(MODE_UTF8)) {
  //   if (ch > 127) {
  //     codepoint[0] = ch;
  //     width = len = 1;
  //   } else {
  //     len = utf8encode(ch, codepoint);
  //     if (!control && (width = wcwidth(ch)) == -1)
  //       width = 1;
  //   }
  // }

  // if (MODE_IS_SET(MODE_PRINT))
  // 	printf("%c", ch);

  /* --- In STR sequence --- */
  if (vt.state & ESC_STR) {
    VTDEBUG("STR Sequence");
    if (ch == BEL || ch == CAN || ch == SUB || ch == ESC || ISCONTROLC1(ch)) {
      vt.state &= ~(ESC_START|ESC_STR);
      vt.state |= ESC_STR_END;
      goto check_control_code;

      /* STR strings are technically infinite but most terminal emulators set
       * hard limits. In our case, the limit has to be the circular buffer. */
      if (vt.str_escape.len+len >= vt.scrollback.buffer_size) {
        return;
      }

      vt.str_escape.len += len;
      return;
    }
  }

check_control_code:
  /* --- Control Sequence Initialzer --- */
  if (control) {
    /* in UTF-8 mode ignore handling C1 control characters */
    if (MODE_IS_SET(MODE_UTF8) && ISCONTROLC1(ch))
      return;

    if (high_bit) vt_handle_C1(ch);
    else vt_handle_C0(ch);

    if (!vt.state)
      vt.last_ch = 0;
    return;
  }

  else if (vt.state & ESC_START) {

    VTTRACE("CSI detected");
    /* --- In CSI Sequence --- */
    if (vt.state & ESC_CSI) {
      vt.csi_escape.len++;
      if (BETWEEN(ch, 0x40, 0x7E) || vt.csi_escape.len > DEL*10) {
        vt.state = 0;
        if (vt_parse_csi()) {
          vt_handle_csi();
        };
      }
      return;
    } else {
      switch (ch) {
        case '[':
          vt.state |= ESC_CSI;
          vt.csi_escape.start = ptr;
          return;
      }
      if (!vt_handle_esc(ch))
        return;
      /* sequence already finished */
    }
    vt.state = 0;
    return;
  } /* end of vt.state & ESC_START */

  vt.last_ch = ch;
  vt_screen_putc(ch);
}

static void
vt_line_feed(Line line) 
{
  char *ptr = vt_scrollback_get(line);
  const char *end = ptr+line.len;
  VTDEBUG("Line Feed: %.*s", line.len, ptr);

  if (line.high_bit) {
    MODE_SET(MODE_UTF8);
  } else {
    MODE_UNSET(MODE_UTF8);
  }

  /*
   * If no 7-bit control codes or high bit (some other control codes are 8-bit)
   * We can skip the state machine altogether if not already in a sequence 
   * This should also represent the majority of cases when i.e. using cat
   */
  if (!(line.control_codes || line.high_bit || (vt.state & ESC_START) ) ) {
    VTDEBUG("ll -> No control Codes or high Bit");
    for (; ptr < end; ptr++)
      vt_screen_putc(*ptr);
    return;
  } 

  /*
   * Has control codes but no high bit (life could be a dream) 
   * We can skip utf-8 and checking for C1 control chars.
   * Aka. we only need to check for C0.
   */
  // if (line.control_codes && !line.high_bit) {
  //   VTDEBUG("ll -> Control Codes, No High Bit");
  //
  //   for (; ptr < end; ptr++) {
  //     assert((unsigned char) *ptr >= 0); 
  //
  //     if (*ptr < SPACE || *ptr == DEL) {
  //       vt_handle_C0(*ptr);
  //     }
  //
  //     /* If the control codes initialized a sequence,
  //      * now we can move to the state machine */
  //     while (VT_IS_SET(ESC_START) && ptr < end) {
  //       vt_char_feed(*ptr, line.control_codes, line.high_bit);
  //       ptr++;
  //     }
  //
  //     vt_screen_putc(*ptr);
  //   }
  //   return;
  // }

  /*
   * Can't think of any more cases to optimize but there might be
   * Last case we just feed all of the bytes to the state machine  
   */

  for (; ptr < end; ptr++) {
    vt_char_feed(ptr, line.control_codes, line.high_bit);
  }

}

static void
vt_parse_input(void) 
{
  /* We call this function any time there is new input from the shell
   * shell_read() -> parse_input() -> writes to logical line buffer
   *
   * New lines are fed through line_feed() to the state machine 
   * updating the screen */

  CBuffer *sc = &vt.scrollback;
  if (sc->read == sc->write-1)
    return;

  const __m128i utf8  = _mm_set1_epi8(0x80);
  const __m128i nl    = _mm_set1_epi8('\n');
  const __m128i esc   = _mm_set1_epi8(0x1B);

  char *ll_beg = sc->buffer + (sc->read % sc->buffer_size);
  char *data   = ll_beg;
  char *ll_end = ll_beg;

  u32 remaining = sc->write - sc->read;

  while (remaining > 0) {
    Line ll = {0, 0, false, false };
    bool found_newline = false;

    /* --- Parse in vector mode --- */
    while (remaining >= 16) {
      __m128i batch = _mm_loadu_si128((const __m128i *)data);
      __m128i test_nl  = _mm_cmpeq_epi8(batch, nl);
      __m128i test_esc = _mm_cmpeq_epi8(batch, esc);
      __m128i test_delim = _mm_or_si128(test_nl, test_esc);
      __m128i test_utf = _mm_and_si128(batch, utf8);

      int delim_mask = _mm_movemask_epi8(test_delim);

      if (delim_mask) {
        
        unsigned int advance = __tzcnt_u32((unsigned int)delim_mask);

        u32 utf_mask = (unsigned)_mm_movemask_epi8(test_utf);
        u32 prefix_mask = 0;
        if (advance > 0) {
          prefix_mask = utf_mask & ((1u << advance) - 1u);
        } 

        if (prefix_mask) {
          ll.high_bit = true;
        }

        data += advance;
        remaining -= (uint32_t)advance;
        ll_end = data;
        break;
      } else {
        unsigned int utf_mask = (unsigned)_mm_movemask_epi8(test_utf);
        if (utf_mask) ll.high_bit = true;

        data += 16;
        remaining -= 16;
        ll_end += 16;
      }
    }

    /* --- Parse in scalar mode --- */
    while (remaining > 0) {
      unsigned char ch = (unsigned char)*data++;
      remaining--;
      ll_end++;

      if (!ll.control_codes && ch == ESC) {
        ll.control_codes = true;
      }
      if (!ll.high_bit && (ch & 0x80u)) {
        ll.high_bit = true;
      }

      if (ch == '\n') {
        found_newline = true;
        break;
      }
    }

    /* --- Push Logical Line --- */
    if (found_newline) {
      ll.len = (size_t)(ll_end - ll_beg);
      ll.start = (size_t)(ll_beg - sc->buffer) - 1;

      VTDEBUG("Pushed ll = [IDX=%04ld\tLEN=%04ld\tC0=%d\tC1=%d]",
          ll.start, ll.len, ll.control_codes, ll.high_bit);

      cbuffer_push_overwrite(&vt.logical_lines, (char *)&ll, sizeof(ll));

      vt_line_feed(ll);

      ll_beg = data;
      ll_end = ll_beg;
    } else {

      ll.len = (size_t)(ll_end - ll_beg);
      ll.start = (size_t)(ll_beg - sc->buffer) - 1;
      cbuffer_push_overwrite(&vt.logical_lines, (char *)&ll, sizeof(ll));
      vt_line_feed(ll);

      break;
    }
  }

  sc->read = sc->write - 1;
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

static bool
vt_parse_csi() 
{
  char *ptr = vt.csi_escape.start+1;
  const char *end = ptr + vt.csi_escape.len;
  VTDEBUG("Parsing CSI string: %.*s", vt.csi_escape.len, ptr);

  i32 value = 0;
  char *np; 
  vt.csi_escape.narg = 0;

	if (*ptr == '?') {
		vt.csi_escape.priv = 1;
		ptr++;
	}

  for (; ptr < end; ptr++) {

		np = NULL;
		value = strtol(ptr, &np, 10);

		if (np == ptr)
			value = 0;

		vt.csi_escape.arg[vt.csi_escape.narg++] = value;
		ptr = np;
		if (*ptr != ';' || vt.csi_escape.narg == ESC_ARG_SIZ)
			break;
  }

  if (*ptr < 0x40 || *ptr > 0x7E) {
    VTWARN("vt_parse_csi: invalid final byte: 0x%02x", *ptr);
    memset(&vt.csi_escape, 0, sizeof vt.csi_escape);
    return false;
  }

  vt.csi_escape.mode[0] = *ptr;
  vt.csi_escape.mode[1] = (ptr+1 < end) ? *(ptr+1) : '\0';
  VTDEBUG("Parsing CSI with mode %.*s", 2, vt.csi_escape.mode);
  return true;
}

static void
vt_handle_csi()
{
  switch (vt.csi_escape.mode[0]) {
    default:
      VTWARN("CSI MODE INVALID OR NOT SUPPORTED: %c", vt.csi_escape.mode[0]);
      vt.csi_escape = (CSIEscape) {0};
      break;
    case '@': 
      VTTRACE("ICH -- Insert %d blank char", vt.csi_escape.arg[0]);
      DEFAULT(vt.csi_escape.arg[0], 1);
      vt_insert_blank(vt.csi_escape.arg[0]);
      break;
    case 'A':
      VTTRACE("CUU -- Cursor %d Up");
      DEFAULT(vt.csi_escape.arg[0], 1);
      vt_move_to(vt.cursor.x, vt.cursor.y-vt.csi_escape.arg[0]);
      break;
    case 'B': /* CUD */
    case 'e': /* VPR */
      VTTRACE("CUD/VPR -- Cursor %d Down", vt.csi_escape.arg[0]);
      DEFAULT(vt.csi_escape.arg[0], 1);
      vt_move_to(vt.cursor.x, vt.cursor.y+vt.csi_escape.arg[0]);
      break;
      /* TODO? */
      /* case 'i': Media Copy 
       break; */
    case 'c': /* DA -- Device Attributes */
      /* TODO? */
      VTTRACE("DA -- Device Attributes");
      // if (vt.csi_escape.arg[0] == 0)
        // vt_scrollback_push(char *str, size_t len)
        // ttywrite(vtiden, strlen(vtiden), 0);
      break;
    case 'b': /* REP -- if last char is printable print it <n> more times */
      VTTRACE("REP -- Print %d Forward", vt.csi_escape.arg[0]);
      DEFAULT(vt.csi_escape.arg[0], 1);
      if (vt.last_ch > 0)
        while (vt.csi_escape.arg[0]-- > 0)
          vt_screen_putc(vt.last_ch);
      break;
    case 'C': /* CUF -- Cursor <n> Forward */
    case 'a': 
      VTTRACE("CUF/HPR -- Cursor %d Forward", vt.csi_escape.arg[0]);
      DEFAULT(vt.csi_escape.arg[0], 1);
      vt_move_to(vt.cursor.x+vt.csi_escape.arg[0], vt.cursor.y);
      break;
      VTTRACE("CUB -- Cursor %d Backward", vt.csi_escape.arg[0]);
      DEFAULT(vt.csi_escape.arg[0], 1);
      vt_move_to(vt.cursor.x-vt.csi_escape.arg[0], vt.cursor.y);
      break;
    case 'E': 
      VTTRACE("CNL -- Cursor %d Down and first col", vt.csi_escape.arg[0]);
      DEFAULT(vt.csi_escape.arg[0], 1);
      vt_move_to(0, vt.cursor.y+vt.csi_escape.arg[0]);
      break;
    case 'F': 
      VTTRACE("CPL -- Cursor %d Up and first col", vt.csi_escape.arg[0]);
      DEFAULT(vt.csi_escape.arg[0], 1);
      vt_move_to(0, vt.cursor.y-vt.csi_escape.arg[0]);
      break;
    case 'g': 
      VTTRACE("TBC -- Tabulation clear");
      /* TODO */
      // switch (vt.csi_escape.arg[0]) {
      //   case 0: /* clear current tab stop */
      //     vt.tabs[vt.cursor.x] = 0;
      //     break;
      //   case 3: /* clear all the tabs */
      //     memset(vt.tabs, 0, vt.cursorol * sizeof(*vt.tabs));
      //     break;
      //   default:
      //     goto unknown;
      // }
      break;
    case 'G': 
    case '`': /* HPA */
      VTTRACE("CHA -- Move to %d", vt.csi_escape.arg[0]);
      DEFAULT(vt.csi_escape.arg[0], 1);
      vt_move_to(vt.csi_escape.arg[0]-1, vt.cursor.y);
      break;
    case 'H': /* CUP -- Move to <row> <col> */
    case 'f': /* HVP */
      DEFAULT(vt.csi_escape.arg[0], 1);
      DEFAULT(vt.csi_escape.arg[1], 1);

      vt_move_to_absolute(vt.csi_escape.arg[1]-1, vt.csi_escape.arg[0]-1);
      break;
    case 'I': /* CHT -- Cursor Forward Tabulation <n> tab stops */
      // DEFAULT(vt.csi_escape.arg[0], 1);
      // tputtab(vt.csi_escape.arg[0]);
      break;
    case 'J': /* ED -- Clear screen */
      // switch (vt.csi_escape.arg[0]) {
      //   case 0: /* below */
      //     tclearregion(vt.cursor.x, vt.cursor.y, vt.cursorol-1, vt.cursor.y);
      //     if (vt.cursor.y < vt.row-1) {
      //       tclearregion(0, vt.cursor.y+1, vt.cursorol-1,
      //           vt.row-1);
      //     }
      //     break;
      //   case 1: /* above */
      //     if (vt.cursor.y > 1)
      //       tclearregion(0, 0, vt.cursorol-1, vt.cursor.y-1);
      //     tclearregion(0, vt.cursor.y, vt.cursor.x, vt.cursor.y);
      //     break;
      //   case 2: /* all */
      //     tclearregion(0, 0, vt.cursorol-1, vt.row-1);
      //     break;
      //   default:
      //     goto unknown;
      // }
      break;
    case 'K': /* EL -- Clear line */
      // switch (vt.csi_escape.arg[0]) {
      //   case 0: /* right */
      //     tclearregion(vt.cursor.x, vt.cursor.y, vt.cursorol-1,
      //         vt.cursor.y);
      //     break;
      //   case 1: /* left */
      //     tclearregion(0, vt.cursor.y, vt.cursor.x, vt.cursor.y);
      //     break;
      //   case 2: /* all */
      //     tclearregion(0, vt.cursor.y, vt.cursorol-1, vt.cursor.y);
      //     break;
      // }
      break;
    case 'S': /* SU -- Scroll <n> line up */
      // if (vt.csi_escape.priv) break;
      // DEFAULT(vt.csi_escape.arg[0], 1);
      // tscrollup(vt.top, vt.csi_escape.arg[0]);
      break;
    case 'T': /* SD -- Scroll <n> line down */
      // DEFAULT(vt.csi_escape.arg[0], 1);
      // tscrolldown(vt.top, vt.csi_escape.arg[0]);
      break;
    case 'L': /* IL -- Insert <n> blank lines */
      // DEFAULT(vt.csi_escape.arg[0], 1);
      // tinsertblankline(vt.csi_escape.arg[0]);
      break;
    case 'l': /* RM -- Reset Mode */
      // tsetmode(vt.csi_escape.priv, 0, vt.csi_escape.arg, vt.csi_escape.narg);
      break;
    case 'M': /* DL -- Delete <n> lines */
      // DEFAULT(vt.csi_escape.arg[0], 1);
      // tdeleteline(vt.csi_escape.arg[0]);
      break;
    case 'X': /* ECH -- Erase <n> char */
      // DEFAULT(vt.csi_escape.arg[0], 1);
      // tclearregion(vt.cursor.x, vt.cursor.y,
      //     vt.cursor.x + vt.csi_escape.arg[0] - 1, vt.cursor.y);
      break;
    case 'P': /* DCH -- Delete <n> char */
      // DEFAULT(vt.csi_escape.arg[0], 1);
      // tdeletechar(vt.csi_escape.arg[0]);
      break;
    case 'Z': /* CBT -- Cursor Backward Tabulation <n> tab stops */
      // DEFAULT(vt.csi_escape.arg[0], 1);
      // tputtab(-vt.csi_escape.arg[0]);
      break;
    case 'd': /* VPA -- Move to <row> */
      // DEFAULT(vt.csi_escape.arg[0], 1);
      vt_move_to_absolute(vt.cursor.x, vt.csi_escape.arg[0]-1);
      break;
    case 'h': /* SM -- Set terminal mode */
      // tsetmode(vt.csi_escape.priv, 1, vt.csi_escape.arg, vt.csi_escape.narg);
      break;
    case 'm': 
      VTDEBUG("SGR -- Terminal attribute (color) %d %d", vt.csi_escape.arg[0], vt.csi_escape.arg[1]);
      vt_cursor_set(vt.csi_escape.arg, vt.csi_escape.narg);
      break;
    case 'n': /* DSR -- Device Status Report */
      switch (vt.csi_escape.arg[0]) {
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
      // if (vt.csi_escape.priv) {
      // 	goto unknown;
      // } else {
      // 	DEFAULT(vt.csi_escape.arg[0], 1);
      // 	DEFAULT(vt.csi_escape.arg[1], vt.row);
      // 	tsetscroll(vt.csi_escape.arg[0]-1, vt.csi_escape.arg[1]-1);
      vt_move_to_absolute(0, 0);
      // }
      break;
    case 's': /* DECSC -- Save cursor position (ANSI.SYS) */
      // tcursor(CURSOR_SAVE);
      break;
    case 'u': /* DECRC -- Restore cursor position (ANSI.SYS) */
      // tcursor(CURSOR_LOAD);
      break;
    case ' ':
      // switch (vt.csi_escape.mode[1]) {
      // case 'q': /* DECSCUSR -- Set Cursor Style */
      // 	if (xsetcursor(vt.csi_escape.arg[0]))
      // 		goto unknown;
      // 	break;
      // default:
      // 	goto unknown;
      // }
      break;
  }
}


static void
vt_handle_str() 
{
  char *ptr = vt.str_escape.start;
  const char *end = ptr + vt.str_escape.len;

  switch (vt.str_escape.type) {
    case ']': /* OSC */
      break;
    case 'P': /* DCS */
      break;
    case '_': /* APC */
      break;
    case '^': /* PM */
      break;
  }
}

static Terminal_Cell*
vt_get_cell_from_xy(u32 x, u32 y) 
{
  /* NOTE: maybe this should be an assert */
  Screen s = (MODE_IS_SET(MODE_ALTSCREEN)) ? vt.alt : vt.screen;
  x = MIN(x, s.cols-1);
  y = MIN(y, s.rows-1);
  return s.cell_buffer + (y*s.cols) + x;

}

static Terminal_Cell*
vt_get_cell_from_cursor(void)
{
  Screen *s = (MODE_IS_SET(MODE_ALTSCREEN)) ? &vt.alt : &vt.screen;
  return s->cell_buffer + (vt.cursor.y*s->cols) + vt.cursor.x;
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

  u32 new_x = MIN(vt.cursor.x + n, vt.screen.cols-1);
  Terminal_Cell *left  = vt_get_cell_from_cursor();
  Terminal_Cell *right = left + n;
  Terminal_Cell *end   = vt_get_cell_from_xy(new_x, vt.cursor.y);

  for (;right <= end; left++, right++ ) {
    left->is_dirty = true;
    *right = *left;
    left->codepoint = (codepoint_t) ' ';
  }

  /* In case right > end (ALL characters are discarded)
   * we continue until left = end */

  for (; left <= end; left++) {
    left->is_dirty = true;
    left->codepoint = (codepoint_t) ' ';
  }
}

static void
vt_move_to(u32 x, u32 y)
{
  vt.cursor.x = MIN(x, vt.screen.cols-1);
  vt.cursor.y = MIN(y, vt.screen.rows-1);
}

static void
vt_move_to_absolute(u32 x, u32 y) 
{
  vt.cursor.x = MIN(x, vt.screen.cols-1);
  vt.cursor.y = BETWEEN(y, vt.bot, vt.top);
}

static void
vt_cmd_putc(codepoint_t c) 
{
  switch (c) {
    default:
      if (vt.cmd_pos < CMD_SIZ) {
        vt_screen_putc(c);
        cmd_buf[vt.cmd_pos++] = c;
      }
      break;
    case '\r':
    case '\n': 
      {
        cmd_buf[vt.cmd_pos++] = '\n';
        vt_sh_write(cmd_buf, vt.cmd_pos);
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

  vt_parse_input();

  return r;
}


static size_t
vt_sh_write(const char * const src, size_t len) 
{
  VTDEBUG("Writing %.*s to the shell", len-1, src);
  ssize_t r = write(vt.sh_fd, src, len);
  if (r < 0) { 
    // ttyputs("Failed to write to the shell");
    VTFATAL("Failed to write to the shell");
  }

  // TODO: while peek read
  vt_sh_read();

  vt_parse_input();

  vt_scrollback_push("\n", 1);
  return r;
}

int
main(void)
{

  log_init();

  if (!renderer_init()) {
    VTFATAL("Failed to initalize renderer!");
    renderer_destroy();
    return 1;
  }

  if (!vt_init(200, 50)) {
    VTFATAL("Failed to initalize terminal!");
    vt_destroy();
    renderer_destroy();
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
          // vt_screen_putc(event.text.text[0]);
          break;
        case SDL_EVENT_WINDOW_RESIZED:
          {
            renderer_resize(event.display.data1, event.display.data2);
            u32 cols, rows;
            renderer_get_grid(&renderer, &cols, &rows);
            vt_resize(cols, rows);
          }
          break;
        case SDL_EVENT_QUIT:
          running = false;
      }
    }

    renderer_draw_screen(MODE_IS_SET(MODE_ALTSCREEN) ? &vt.alt : &vt.screen);
    renderer_sync();
    frames++;

    if (frames % 60 == 0) {
      gettimeofday(&end, NULL);
      double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
      char buf[128];
      snprintf(buf, 128, "vt - %ux%u %.2f FPS", vt.screen.cols, vt.screen.rows, frames / elapsed );
      SDL_SetWindowTitle(context.window, buf);
    }

    SDL_Delay(12);
  }

  VTDEBUG("Frames: %ld\n", frames);

  vt_destroy();
  renderer_destroy();
  log_destroy();

  VTINFO("Quit successfully!");
  return 0;
}

