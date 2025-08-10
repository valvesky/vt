#pragma once
/* ---------------------------------------------------------------------------
 * VT Design 
 * ---------------------------------------------------------------------------
 * To make sure this terminal lasts forever and works everywhere, we want 
 * a good platform layer and good 'black boxing.' Aka. components should be
 * interchangeable without requiring knowledge of each other. A terminal emulator
 * is pretty simple thankfully. It's basically a parser / state machine
 * hooked up to a renderer. 
 *
 * What the renderer does we do not care.
 * The state machine should be able to work regardless of the renderer we choose
 * as long as the API to draw to the screen is implemented.
 *
 * +---------------+
 * | State Machine | 
 * +---------------+
 *     TTY API                         
 * +-------------+ +-----------------+
 * | Renderer    |-| Glyth Generator |
 * +-------------+-+-----------------+
 * | Platform Layer (SDL3)           |
 * +---------------------------------+
 * 
 * Another reason for the modularity and "over-design" is to avoid the major
 * suckless software hypocrisy of "patchable" code that is sometimes unreadable
 * breaks easily and overrall is just very finicky to patch.
 *
 * If you want to patch a certain module, you should be able to assume 
 * everything else is a black box that just works!
 *
 * If someone wants to patch a new shader for my terminal to make it CRT-like
 * they should be able to pick a renderer, change what they want to change,
 * and that's it.
 *
 * And if something does break, they should be able to tell from the debug
 * information. 
 *
 * Thank you for reading.
 */

#include "vt_platform.h"

/* ---------------------------------------------------------------------------
 * Terminal State Machine
 * --------------------------------------------------------------------------- */

/* The terminal is a procedural black box that takes in bytes and makes
 * draw calls to a renderer. It does not need to store information about
 * the screen, only the scrollback, cursor, state, etc. */
typedef struct Terminal Terminal;
typedef struct Screen Screen;
typedef struct Renderer_Cell Renderer_Cell;

typedef u32 color_t;

struct Screen {
  Renderer_Cell *cell_buffer;
  u32 rows;
  u32 cols;
  u32 capacity;
};

typedef struct {
  color_t fg; // 8 bits for flags
  color_t bg; // 1 bit for blinking
  u32 x, y;
  u8 state;
} Terminal_Cursor;

/* TODO: this struct could probably be optimized
to include more pre-processing data */
typedef struct LogicalLine {
  u64 start;
  u32 len;
  u32 codepoint_len;
  bool has_ansi;
  bool has_unicode;
} LogicalLine;

struct Terminal {
  Screen *screen;         /* 2D array of cells, the render takes care of displaying */
  CBuffer scrollback;     /* main circular buffer with input from shell and user */
  CBuffer logical_lines;  /* circular buffer with indexes and pre-processing info for logical lines */

  i32 sh_pid;
  i32 sh_fd; 

  Terminal_Cursor cursor; /* position in space and draw state */

  u32 state;
  u16 cmd_pos;  /* position of cursor inside cmd */
  u16 cmd_len;  /* length of cmd  */
};

extern bool vt_init(Screen*); 
// extern void vt_putc(u8);
// extern void vt_puts(u8*, u32);
extern void vt_destroy(void);

typedef struct Renderer Renderer;
extern bool renderer_init(Screen*);
extern void renderer_draw_codepoint(codepoint_t c, u32 x, u32 y, color_t fg, color_t bg);

extern void renderer_insert_space(u32 n, u32 x, u32 y);
extern void renderer_insert_newline(u32 n, u32 x, u32 y);

extern void renderer_copy(u32 x1, u32 y1, u32 x2, u32 y2);
extern void renderer_resize(u32 width, u32 height);
extern void renderer_sync(void);
extern void renderer_clear(void);
extern void renderer_destroy(void);

