#pragma once
/*
 * ASCII art is the fitting way to explain a terminal emulator.
 *
 * ,--------------------------,
 * | Shell (PTY) / Named Pipe |
 * '-------------+------------'
 *               |               (use SIMD)               ,-------------,
 *         ,-----+------,      ,--------------, Lines ,-> | Line Buffer | (For Scrollback)
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

/* VT500 C0 Codes */
#define NUL   0x00
#define SOH   0x01
#define STX   0x02
#define ETX   0x03
#define EOT   0x04
#define ENQ   0x05
#define ACK   0x06
#define BEL   0x07
#define BS    0x08
#define HT    0x09
#define LF    0x0A
#define VT    0x0B
#define FF    0x0C
#define CR    0x0D
#define SO    0x0E
#define SI    0x0F
#define DLE   0x10
#define DC1   0x11
#define DC2   0x12
#define DC3   0x13
#define DC4   0x14
#define NAK   0x15
#define SYN   0x16
#define ETB   0x17
#define CAN   0x18
#define EM    0x19
#define SUB   0x1A
#define ESC   0x1B
#define FS    0x1C
#define GS    0x1D
#define RS    0x1E
#define US    0x1F
#define DEL   0x7F

typedef u32 color_packed_t;

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
  u32 codepoint;
  bool is_dirty;
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
  bool control_codes;
  bool high_bit;
} Line;

/* Control Sequences */
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
  char *start; //  ptr to scrollback
	u32 len;   
  i32 arg[ESC_ARG_SIZ];
  i32 narg; /* number of args */
  char priv;
  char mode[2];
} CSIEscape;

typedef struct {
  char *start; //  ptr to scrollback
  u32 len;   // len
  char *args[STR_ARG_SIZ];
  int narg; /* nb of args */
	char type; 
} STREscape;

typedef struct Terminal {
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
} Terminal;

/* ---------------------------------------------------------------------------
 * Renderer
 * --------------------------------------------------------------------------- */

typedef struct Renderer Renderer;
typedef struct Renderer_Cell Renderer_Cell;
typedef u32 color_packed_t;
