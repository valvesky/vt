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

/* It's best to treat vt as a state machine instead of passing
 * i.e. Terminal *term in every function.
 *
 * I don't intent to support multiplexing and I don't want to feel
 * guilty for using the stack and global variables to keep track of state.
 *
 * Same goes for the renderer. */

// static bool terminal_init(Screen*);
// static bool terminal_handle_key(char *src, size_t len);
// static bool terminal_line_feed(char *src, size_t len);
// static void terminal_destroy();
//
// static bool renderer_init(Renderer*, Screen*);
// static void renderer_draw_screen(Renderer*);
// static void renderer_resize_screen(Renderer*, int width, int height);
// static void renderer_destroy(Renderer*);

/* We are going to use A LOT of uint32 mainly because of packing data
 * for the renderer. It will be the default type over something like an int. */
typedef unsigned int u32; 
static_assert(sizeof (u32) == 4, "u32 must be 4 bytes"); 

typedef unsigned short u16;
static_assert(sizeof (u16) == 2, "u16 must be 2 bytes"); 

typedef unsigned char u8;
static_assert(sizeof (u8) == 1, "u8 must be 1 byte"); 

#define MIN(a, b)		((a) < (b) ? (a) : (b))
#define MAX(a, b)		((a) < (b) ? (b) : (a))
#define LEN(a)			(sizeof(a) / sizeof(a)[0])
#define BETWEEN(x, a, b)	((a) <= (x) && (x) <= (b))
#define DIVCEIL(n, d)		(((n) + ((d) - 1)) / (d))
#define DEFAULT(a, b)		(a) = (a) ? (a) : (b)
#define LIMIT(x, a, b)		(x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x)

/* --- Terminal --- */

typedef u8 utf8_t;

/* NOTE: this is not used YET but there could be terminal-specific
 * features that different renderers enable / disable in the future */
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
  u32 fg; // 8 bits for flags
  u32 bg; // 1 bit for blinking
  u32 x, y;
} Terminal_Cursor;

typedef struct {
  u32 codepoint;
  u32 fg; // 8 bits for flags
  u32 bg; // 1 bit for blinking
  bool is_dirty;
} Terminal_Cell;

struct Screen {
  Terminal_Cell *cell_buffer;
  u32 rows;
  u32 cols;
};

struct Terminal {
  Screen *screen;         /* 2D array of cells, the render takes care of displaying */
  CBuffer scrollback;     /* main circular buffer with input from shell and user */
  CBuffer logical_lines;  /* circular buffer with indexes and pre-processing info for logical lines */
  Shell shell;            /* the shell */
  Terminal_Cursor cursor; /* position in space and draw state */

  u32 state;
  u16 cmd_pos;  /* position of cursor inside cmd */
  u16 cmd_len;  /* length of cmd  */
};

/* --- Renderer --- */

#define RENDERER_CELL_BLINK 0x80000000

typedef struct {
  u32 pos;
  u32 glyth_index;
  u32 foreground; 
  u32 background;  
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
