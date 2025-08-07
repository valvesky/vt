#pragma once
/* A terminal is basically a parser that updates a screen.
 * And keeps track of what cells to update ("dirty" cells).
 *
 * The renderer takes those cells and updates the buffer of instances
 * to be drawn on the GPU.
 *
 *            Updates        Displays
 * | Terminal | --> | Screen | <-- | Renderer |
 *  - Shell                         - Glyth Generator
 *  - State                         - Context
 *  - Scrollback                    - Atlas
 *
 * This design avoid some data duplication between the renderer
 * and the terminal, namely duplicate columns and rows.
*/

typedef struct Terminal Terminal;
typedef struct Screen   Screen;
typedef struct Renderer Renderer;

static bool terminal_init(Terminal*, Screen*);
// static bool terminal_handle_key(Terminal *t, char *src, size_t len);
// static bool terminal_line_feed(Terminal *t, char *src, size_t len);
static void terminal_destroy(Terminal*);

static bool renderer_init(Renderer*, Screen*);
static void renderer_draw_screen(Renderer*);
static void renderer_resize_screen(Renderer*, int width, int height);
static void renderer_destroy(Renderer*);

/* --- Terminal --- */
typedef enum {
  TERM_REND_OPENGL3,
  TERM_REND_VULKAN
} Renderer_Type;

typedef enum {
  TERM_CURSOR_CMD = 0,
  TERM_CURSOR_DRAW,
} Cursor_State;

/* TODO: this struct could probably be optimized
to include more pre-processing data */
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

typedef struct {
  uint32_t fg; // 8 bits for flags
  uint32_t bg; // 1 bit for blinking
  uint32_t x, y;
} Terminal_Cursor;

typedef struct {
  uint32_t codepoint;
  uint32_t fg; // 8 bits for flags
  uint32_t bg; // 1 bit for blinking
  bool is_dirty;
} Terminal_Cell;

struct Screen {
  Terminal_Cell *cell_buffer;
  uint32_t rows;
  uint32_t cols;
};

struct Terminal {
  Screen *screen;         /* 2D array of cells, the render takes care of displaying */
  CBuffer scrollback;     /* main circular buffer with input from shell and user */
  CBuffer logical_lines;  /* circular buffer with indexes and pre-processing info for logical lines */
  Shell shell;            /* the shell */
  Terminal_Cursor cursor; /* position in space and draw state */

  uint16_t cmd_pos;  /* position of cursor inside cmd */
  uint16_t cmd_len;  /* length of cmd  */
  uint8_t state;     /* rendering state */
};

/* --- Renderer --- */

#define RENDERER_CELL_BLINK 0x80000000

typedef struct {
  uint32_t pos;
  uint32_t glyth_index;
  uint32_t foreground; 
  uint32_t background;  
} Renderer_Cell;

typedef enum {
  R_CELL_ATTR_POS = 0,
  R_CELL_ATTR_GLYTH_INDEX,
  R_CELL_ATTR_FG,
  R_CELL_ATTR_BG,
  ATTR_COUNT_RENDERER
} Renderer_Cell_Attr;

static const size_t attr_offset_array[ATTR_COUNT_RENDERER] = {
  [R_CELL_ATTR_POS] = offsetof(Renderer_Cell, pos),
  [R_CELL_ATTR_GLYTH_INDEX] = offsetof(Renderer_Cell, glyth_index),
  [R_CELL_ATTR_FG] = offsetof(Renderer_Cell, foreground),
  [R_CELL_ATTR_BG] = offsetof(Renderer_Cell, background),
};

static_assert(ATTR_COUNT_RENDERER == 4, "Renderer Cell Attributes has changed, update code and shaders");
