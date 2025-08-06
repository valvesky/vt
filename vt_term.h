#ifndef _VT_TERM_H_
#define _VT_TERM_H_

typedef struct {
  uint64_t start;
  size_t len;
  size_t codepoint_len;
  bool has_unicode;
  bool has_ansi;
} LogicalLine;

typedef struct {
  int pid;
  int fd;
  bool active;
} Shell;

typedef enum {
  TERM_CURSOR_CMD = 0,
  TERM_CURSOR_DRAW,
} Cursor_State;

typedef struct {
  uint32_t fg; // 8 bits for flags
  uint32_t bg; // 1 bit for blinking
  uint32_t x, y;
} Terminal_Cursor;

typedef struct Terminal {

  Renderer *renderer; // we draw directly to the renderer's buffer
  
  CBuffer scrollback;    /* main circular buffer with input from shell and user */
  CBuffer logical_lines; /* circular buffer with indexes of logical lines */

  Shell shell; // shell

  Terminal_Cursor cursor; // cursor in x,y space, contains draw state

  uint16_t cmd_pos;  // position of cursor inside cmd
  uint16_t cmd_len; // length of cmd 
  uint8_t state;
} Terminal;

typedef struct Terminal* term_t;

/* API */
Terminal Terminal_Create(Renderer*);
void     Terminal_Destroy(Terminal*);
void     Terminal_CMD_Write(Terminal*, const char*, size_t);
void     Terminal_CMD_Backspace(Terminal*);
void     Terminal_CMD_Left(Terminal*);
void     Terminal_CMD_Right(Terminal*);
void     Terminal_CMD_Up(Terminal*);
void     Terminal_CMD_Down(Terminal*);

#endif
