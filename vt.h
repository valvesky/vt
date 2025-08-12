#pragma once

#include "vt_platform.h"

/* ---------------------------------------------------------------------------
 * Terminal State Machine
 * --------------------------------------------------------------------------- */

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
  color_t fg; 
  color_t bg;
  u32 x, y;
  u8 attr;
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
  char last_ch;
};

typedef struct Renderer Renderer;
extern bool renderer_init(Screen*);
extern void renderer_draw_codepoint(codepoint_t c, u32 x, u32 y, color_t fg, color_t bg, u8 attr);

extern void renderer_insert_space(u32 n, u32 x, u32 y);
extern void renderer_insert_newline(void);
extern void renderer_copy(u32 x1, u32 y1, u32 x2, u32 y2);

extern void renderer_resize(u32 width, u32 height);
extern void renderer_sync(void);
extern void renderer_clear(void);
extern void renderer_destroy(void);

