#ifndef _VT_TERM_H_
#define _VT_TERM_H_

#include "vt_opengl.h"
#include "vt_circ_buf.c"
#include "vt_vec.h"
#include "config.h"

typedef struct LL {
  uint64_t start;
  uint64_t len;
  bool has_unicode;
  bool has_ansi;
} LogicalLine;


typedef struct {
  int pid;
  int fd;
  bool active;
} Shell;

typedef enum {
  CURSOR_DRAW,
  CURSOR_CMD
} Cursor_State;

typedef struct {
} DrawState; 

typedef struct {
  vec4f fg;
  vec4f bg;
  uint16_t flags;
  uint32_t x, y;
} Terminal_Cursor;

typedef struct Terminal {

  Renderer renderer; // we draw directly to the renderer's buffer
  
  CBuffer scrollback;    /* main circular buffer with input from shell and user */
  CBuffer logical_lines; /* circular buffer with indexes of logical lines */

  Shell shell; // shell

  Terminal_Cursor cursor_real;
  uint16_t cursor; // position of cursor inside cmd
  uint16_t cmd_len; // length of cmd 

} Terminal;

typedef struct Terminal* term_t;

#endif
