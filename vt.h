#pragma once
/*
 * ASCII art is the fitting way to explain a terminal emulator.
 *
 * ,--------------------------,
 * | Shell (PTY) / Named Pipe |
 * '-------------+------------'
 *               |               (use SIMD)               ,-------------,
 *         ,-----+------,      ,--------------, Lines ,-> | Line Buffer | (Scrollback)
 *         | Scrollback | ---> | preprocessor | ------+   :=============:
 *         '------------'      '--------------'       '-> | Line Feed   | (Updates State Machine)
 *        (Circular Buffer)                               '------+------'
 *                                                               |
 *                   ,--------,  Glyth  ,------------,      ,----+-----, (Virtual Screen State)
 *                   | Shader | <------ |  Renderer  | <--- |  Screen  | (There is also an alt screen)
 *                   '--------'  Cache  '------------'      '----------' 
 *
 * Basically we parse new input into lines and keep track of which have
 * codes and/or utf-8. This avoids doing extra work in the line feed parser.
 * SSE2 (Single Instruction Multiple Data) speeds up this process tremendously.
 *
 * We can then either render all new lines or render only the tail of the input.
 *
 * To render the tail we simply skip making draw calls to the renderer.
 * The rendering is always the most expensive part even with such a simple
 * shader.
 */

#include "vt_platform.h"

/* ---------------------------------------------------------------------------
 * Terminal State Machine
 * --------------------------------------------------------------------------- */

enum Term_Mode {
	MODE_WRAP        = 1 << 0,
	MODE_INSERT      = 1 << 1,
	MODE_ALTSCREEN   = 1 << 2,
	MODE_CRLF        = 1 << 3,
	MODE_ECHO        = 1 << 4,
	MODE_PRINT       = 1 << 5,
	MODE_UTF8        = 1 << 6,
};

enum Term_State {
	ESC_START      = 1,
	ESC_CSI        = 2,
	ESC_STR        = 4,
	ESC_ALTCHARSET = 8,
	ESC_STR_END    = 16, 
	ESC_TEST       = 32,
	ESC_UTF8       = 64,
};

enum Cursor_Movement {
	CURSOR_SAVE,
	CURSOR_LOAD
};

enum Cursor_State {
	CURSOR_DEFAULT  = 0,
	CURSOR_WRAPNEXT = 1,
	CURSOR_ORIGIN   = 2
};

typedef enum {
    ATTR_NONE       = 0,
    ATTR_BOLD       = 1 << 0,
    ATTR_FAINT      = 1 << 1,
    ATTR_ITALIC     = 1 << 2,
    ATTR_UNDERLINE  = 1 << 3,
    ATTR_BLINK      = 1 << 4,
    ATTR_REVERSE    = 1 << 5,
    ATTR_INVISIBLE  = 1 << 6,
    ATTR_STRUCK     = 1 << 7,
} Cursor_Attr;


typedef struct {
  u32 fg; 
  u32 bg;
  u32 x, y;
  u8 attr;
  u8 state;
} Terminal_Cursor;

typedef struct {
  u32 fg;     /* packed attributes */
  u32 bg;     /* packed attributes */
  u32 glyth;
} Terminal_Cell;

typedef struct {
  Terminal_Cell *cell_buffer;
  u32 cols;
  u32 rows;
  u32 capacity;
} Screen;

/* NOTE: this struct could probably be optimized
   to include more pre-processing data namely how many
   visual glyths the line ocupies */
typedef struct Line {
  u64 start;
  u32 len;
  // u32 glyth_len;
  bool has_ansi;
  bool has_unicode;
} Line;

/* Control Sequences */
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

typedef struct {
  char priv;
  int arg[ESC_ARG_SIZ];
  int narg; /* number of args */
  char mode[2];
} CSIEscape;

typedef struct {
	char type; /* ESC type ... */
	char *buf; /* allocated raw string */
	size_t siz; /* allocation size */
	size_t len; /* raw string length */
	char *args[STR_ARG_SIZ];
	int narg; /* nb of args */
} STREscape;

struct Terminal {
  CBuffer scrollback;     /* main circular buffer with input from shell and user */
  CBuffer logical_lines;  /* circular buffer with indexes and pre-processing info for logical lines */
  Screen screen;         /* main screen */
  Screen alt;            /* alt screen */
  CSIEscape csi_escape;
  STREscape str_escape;
	u32 top;                /* top scroll limit (index to logical line with smallest start) */
	u32 bot;                /* bottom scroll limit (the last logical line) */
  i32 sh_pid;             /* shell pid */
  i32 sh_fd;              /* shell fd */
  Terminal_Cursor cursor; /* position in space and draw state */
  u32 mode;               /* terminal mode */
  u32 state;              /* terminal escape state */
  u16 cmd_pos;            /* position of cursor inside cmd */
  u16 cmd_len;            /* length of cmd  */
  char last_ch;           /* last char */
};

/* ---------------------------------------------------------------------------
 * Renderer
 * --------------------------------------------------------------------------- */

typedef struct Renderer Renderer;

extern bool renderer_init(Screen*);
extern void renderer_draw_codepoint(codepoint_t c, u32 x, u32 y, color_t fg, color_t bg, u8 attr);
extern void renderer_resize(u32 width, u32 height);
extern void renderer_sync(void);
extern void renderer_clear(void);
extern void renderer_destroy(void);

