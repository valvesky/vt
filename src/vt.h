#pragma once

#define VT_MAJOR 0
#define VT_MINOR 7
#define VT_PATCH 11

/* CHANGE LOG
 * 0.1.0 - @vasco - Peak Rend Term; ctl; headless
 * 0.1.1 - @vasco - SGR/X10 wheel when child enables mouse
 * 0.1.2 - @vasco - win32 headless compile
 * 0.1.3 - @vasco - SSE ascii runs; draw from TermCell.is_dirty
 * 0.2.0 - @vasco - ring+prepass; TermCell SSBO; slang BDA present; atlas LRU
 * 0.2.1 - @vasco - cellMain dest blit; glyph get on UTF-8 ingest
 * 0.2.2 - @vasco - typed Term feed: printable / utf8 / escape / any
 * 0.2.3 - @vasco - VtRun type: printable / escape / utf8; no mixed feed
 * 0.2.4 - @vasco - utf8 atoms do not swallow ASCII; CSI ASCII stays in the escape run
 * 0.2.5 - @vasco - drain ring past VT_RUN_MAX before present / wait
 * 0.2.6 - @vasco - config vsync; present begin/record/end ns
 * 0.3.0 - @vasco - instanced glyph quads; drop compute dest blit
 * 0.3.1 - @vasco - ring never drops; ingest until EAGAIN or one frame
 * 0.3.2 - @vasco - event-driven ingest; no 60Hz frame budget
 * 0.3.3 - @vasco - heap Term cells; instance buffer only; bake SGR colors
 * 0.3.4 - @vasco - fill: ascii glyph table, LRU peek, bake fast path
 * 0.3.5 - @vasco - quit avg parse/fill/begin/draw/end/present ns
 * 0.3.6 - @vasco - opaque default; compositor no longer owns present
 * 0.3.7 - @vasco - ingest yields on incomplete ring head; no drain spin
 * 0.4.0 - @vasco - mouse selection, PRIMARY/CLIPBOARD, OSC 52 set, mouse protocol
 * 0.4.1 - @vasco - skip default-bg spaces in fill; CPU raster via Rend 1.6.1
 * 0.4.2 - @vasco - present at most hz; wait timeout is the deadline
 * 0.5.0 - @vasco - 8-byte TermCell fill; bracketed paste; Rend VK knobs
 * 0.5.1 - @vasco - ctl read/rg; latest.sock
 * 0.5.2 - @vasco - fallback font; CBDT color emoji
 * 0.5.3 - @vasco - color emoji draw at two cells
 * 0.5.4 - @vasco - VT_RUN_KITTY; per-pane kitty session
 * 0.5.5 - @vasco - kitty stamp: IND scroll, LRU evict, C=0 at col 0
 * 0.5.6 - @vasco - mux: middle-drag live pane; ctl move
 * 0.5.7 - @vasco - mux pane drop: pointer pid, dest split, give pid
 * 0.5.8 - @vasco - mux drop connect+export-first; X11 XDND finish
 * 0.6.0 - @vasco - config.h mux/clip keys; build skips Vulkan without ICD
 * 0.7.0 - @vasco - vtctl; Wayland pane drop
 * 0.7.1 - @vasco - Omarchy theme file; SIGUSR1 reload; palette at present
 * 0.7.2 - @vasco - hz hold still presents; PTY + ingest every wait
 * 0.7.3 - @vasco - drain rings until EAGAIN or present due
 * 0.7.4 - @vasco - greedy receive then parse; Peak pipe capacity
 * 0.7.5 - @vasco - Term in src/vt_term; bulk printable feed; wait restored; tile present
 * 0.7.6 - @vasco - 128-bit rolling glyph-run hash; combining sequences
 * 0.7.7 - @vasco - row-ring scroll; LF is origin++ not memmove
 * 0.7.11 - @vasco - one vt.h; drop vt_* satellites; unity includes
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <immintrin.h>
#include "stb_truetype.h"

#ifndef MIN
#define MIN(a, b)        ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b)        ((a) > (b) ? (a) : (b))
#endif
#define LEN(a)           (sizeof(a) / sizeof(a)[0])
#define BETWEEN(x, a, b) ( ((unsigned)((x) - (a))) <= (unsigned)((b) - (a)) )

#define BOX(n, e, s, w) ((u8)(((n) << 6) | ((e) << 4) | ((s) << 2) | (w)))

#if defined(__clang__) || defined(__GNUC__)
#define STATIC_ASSERT _Static_assert
#else
#define STATIC_ASSERT static_assert
#endif

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;
typedef int64_t  i64;
typedef int32_t  i32;
typedef int16_t  i16;
typedef int8_t   i8;
typedef float    f32;
typedef double   f64;

STATIC_ASSERT(sizeof (u64) == 8, "u64 must be 8 bytes");
STATIC_ASSERT(sizeof (u32) == 4, "u32 must be 4 bytes");
STATIC_ASSERT(sizeof (u16) == 2, "u16 must be 2 bytes");
STATIC_ASSERT(sizeof (i64) == 8, "i64 must be 8 bytes");
STATIC_ASSERT(sizeof (i32) == 4, "i32 must be 4 bytes");
STATIC_ASSERT(sizeof (i16) == 2, "i16 must be 2 bytes");
STATIC_ASSERT(sizeof (i8) == 1, "i8 must be 1 byte");
STATIC_ASSERT(sizeof (u8) == 1, "u8 must be 1 byte");
STATIC_ASSERT(sizeof (f64) == 8, "f64 must be 8 bytes");
STATIC_ASSERT(sizeof (f32) == 4, "f32 must be 4 bytes");

#pragma GCC poison wchar_t
typedef u32 codepoint_t;
typedef u32 color_packed_t;

#if defined(DEBUG)
#define VTASSERT_N(_1, _2, N, ...) N
#define VTASSERT(...) VTASSERT_N(__VA_ARGS__, VTASSERT2, VTASSERT1)(__VA_ARGS__)
#define VTASSERT1(a) assert(a)
#define VTASSERT2(a, s) assert((a) && (s))
#else
#define VTASSERT(...) ((void)0)
#endif

#define VT_TODO \
  do { \
    fprintf(stderr, "VT TODO: %s() in %s:%d\n", __func__, __FILE__, __LINE__); \
    abort(); \
  } while (0)

/* NOTE(vasco):
 * Printing to stderr avoids clogging stdout during benchmarsk
 */
#define vt_logf(prefix, fmt, ...)\
    fprintf(stderr, prefix" "fmt"\n", ##__VA_ARGS__)

#define VTFATAL(message, ...) vt_logf("[FATAL]", message, ##__VA_ARGS__)
#define VTERROR(message, ...) vt_logf("[ERROR]", message, ##__VA_ARGS__)
#define VTWARN(message, ...)  vt_logf("[WARNI]", message, ##__VA_ARGS__)
#define VTINFO(message, ...)  vt_logf("[INFOR]", message, ##__VA_ARGS__)
#define VTTRACE(message, ...) vt_logf("[TRACE]", message, ##__VA_ARGS__)

#if P_LOG_DEBUG_ENABLED == 1
#define VTDEBUG(message, ...) vt_logf("[DEBUG]", message, ##__VA_ARGS__)
#else
#define VTDEBUG(message, ...)
#endif

#define TERM_MAJOR 0
#define TERM_MINOR 7
#define TERM_PATCH 5

#if defined(TERM_DEBUG)
#define TASSERT_N(_1, _2, N, ...) N
#define TASSERT(...) TASSERT_N(__VA_ARGS__, TASSERT2, TASSERT1)(__VA_ARGS__)
#define TASSERT1(a) assert(a)
#define TASSERT2(a, s) assert((a) && (s))
#else
#define TASSERT(...) ((void)0)
#endif

#define TERM_TODO \
    do { \
        fprintf(stderr, "TERM TODO: %s() in %s:%d\n", __func__, __FILE__, __LINE__); \
        abort(); \
    } while (0)

#define TERM_MIN(a, b) ((a) < (b) ? (a) : (b))
#define TERM_MAX(a, b) ((a) > (b) ? (a) : (b))
#define TERM_BETWEEN(x, a, b) (((unsigned)((x) - (a))) <= (unsigned)((b) - (a)))
#define TERM_DEFAULT(a, b) ((a) = (a) ? (a) : (b))

#define TERM_UTF_INVALID 0xFFFDu

#define TERM_MODE_WRAP      (1u << 0)
#define TERM_MODE_INSERT    (1u << 1)
#define TERM_MODE_ALTSCREEN (1u << 2)
#define TERM_MODE_CRLF      (1u << 3)
#define TERM_MODE_ECHO      (1u << 4)
#define TERM_MODE_PRINT     (1u << 5)
#define TERM_MODE_UTF8      (1u << 6)
#define TERM_MODE_HIDE      (1u << 7)
#define TERM_MODE_MOUSEBTN  (1u << 8)
#define TERM_MODE_MOUSEMOT  (1u << 9)
#define TERM_MODE_MOUSEMANY (1u << 10)
#define TERM_MODE_MOUSESGR  (1u << 11)
#define TERM_MODE_BRKTPASTE (1u << 12)
#define TERM_MODE_MOUSE     (TERM_MODE_MOUSEBTN | TERM_MODE_MOUSEMOT | TERM_MODE_MOUSEMANY)
#define TERM_WRAPNEXT       1u

#define TERM_ESC_START      1u
#define TERM_ESC_CSI        2u
#define TERM_ESC_STR        4u
#define TERM_ESC_ALTCHARSET 8u
#define TERM_ESC_STR_END    16u
#define TERM_ESC_TEST       32u
#define TERM_ESC_UTF8       64u

#define TERM_ATTR_NONE      0u
#define TERM_ATTR_BOLD      (1u << 0)
#define TERM_ATTR_FAINT     (1u << 1)
#define TERM_ATTR_ITALIC    (1u << 2)
#define TERM_ATTR_UNDERLINE (1u << 3)
#define TERM_ATTR_BLINK     (1u << 4)
#define TERM_ATTR_REVERSE   (1u << 5)
#define TERM_ATTR_INVISIBLE (1u << 6)
#define TERM_ATTR_STRUCK    (1u << 7)

#define TERM_COLOR_DEF 0u
#define TERM_COLOR_PAL 1u
#define TERM_COLOR_RGB 2u

#define TERM_ESC_ARG_SIZ 16
#define TERM_CSI_BUF_SIZ 256
#define TERM_HIST_MAX    1024
#define TERM_CELL_CODE   0u
#define TERM_SEQ_MAX     8u
#define TERM_STYLE_MAX   65536u

typedef struct TermColors {
    uint32_t fg[16];
    uint32_t bg[8];
    uint32_t fg_default;
    uint32_t bg_default;
} TermColors;

typedef struct TermCursor {
    uint32_t fg;
    uint32_t bg;
    uint32_t x;
    uint32_t y;
    uint8_t attr;
    uint8_t state;
    uint8_t fg_kind;
    uint8_t bg_kind;
    uint8_t fg_idx;
    uint8_t bg_idx;
    uint16_t style;
} TermCursor;

typedef struct TermStyle {
    uint32_t fg;
    uint32_t bg;
    uint8_t fg_kind;
    uint8_t bg_kind;
    uint8_t fg_idx;
    uint8_t bg_idx;
    uint8_t attr;
} TermStyle;

typedef struct TermCell {
    uint32_t codepoint;
    uint16_t style;
    uint16_t tag;
} TermCell;

typedef struct TermScreen {
    TermCell **line;
    uint32_t *line_n;
    uint32_t *line_cap;
    uint8_t *wrap;
    uint32_t cap;
    uint32_t n;
    uint32_t view;
    uint32_t cols;
    uint32_t rows;
} TermScreen;

typedef struct TermCsi {
    char buf[TERM_CSI_BUF_SIZ];
    int32_t arg[TERM_ESC_ARG_SIZ];
    uint32_t len;
    int32_t narg;
    char priv;
    char mode[2];
} TermCsi;

typedef struct TermStr {
    uint32_t len;
    char type;
} TermStr;

typedef struct Term {
    TermScreen screen;
    TermScreen alt;
    TermCursor cursor;
    TermCursor saved;
    TermColors colors;
    TermCsi csi;
    TermStr str;
    uint32_t top;
    uint32_t bot;
    uint32_t mode;
    uint32_t state;
    uint32_t utf8_acc;
    uint32_t utf8_min;
    uint32_t last_ch;
    uint8_t utf8_rem;
    char reply[256];
    uint32_t reply_n;
    char str_buf[128];
    TermStyle *styles;
    uint16_t *style_hash;
    uint32_t style_n;
    uint32_t style_cap;
    uint32_t style_hash_n;
    int cells_owned;
    int styles_owned;
    uint32_t seq[TERM_SEQ_MAX];
    uint32_t seq_n;
    uint32_t seq_i;
    uint8_t cs_g0;
    uint8_t cs_g1;
    uint8_t cs_gl;
    uint8_t cs_sel;
} Term;

int term_init(Term *t, uint32_t cols, uint32_t rows, const TermColors *colors);
int term_init_on(Term *t, uint32_t cols, uint32_t rows, const TermColors *colors, TermCell *screen, TermCell *alt, uint32_t cap, TermStyle *styles, uint32_t style_cap);
void term_destroy(Term *t);
void term_resize(Term *t, uint32_t cols, uint32_t rows);
void term_resize_on(Term *t, uint32_t cols, uint32_t rows, TermCell *screen, TermCell *alt, uint32_t cap);
void term_feed_printable(Term *t, const char *bytes, size_t len); /* 0x20-0x7E */
void term_feed_utf8(Term *t, const char *bytes, size_t len); /* complete UTF-8, high bytes */
void term_feed_escape(Term *t, const char *bytes, size_t len); /* C0 / ESC / CSI / OSC; 7-bit */
TermScreen *term_screen(Term *t);
TermCell *term_row(TermScreen *s, uint32_t y); // logical row, contiguous cols
TermCell *term_cell_at(TermScreen *s, uint32_t x, uint32_t y);
uint32_t term_hist_count(const Term *t);
const TermCell *term_hist_line(const Term *t, uint32_t back);
TermStyle term_cell_style(const Term *t, const TermCell *c);
TermStyle term_cursor_style(const Term *t);
void term_colors_set(Term *t, const TermColors *colors);

/* NOTE(vasco):
 *
 * The ring is a tail window. New bytes overwrite the oldest when full.
 * A 1GB dump becomes ~size bytes, then last N logical lines.
 *
 * 1) Line prepass: split on NL. Offscreen lines are not parsed.
 * 2) Run prepass: those lines into printable / escape / utf-8 / kitty.
 *    Wrap without NL is later.
 */

/* NOTE(vasco):
 * Indexes into the buffer are 64 bit so we can easily check if a line
 * has been scrolled off!
 * (buffer_idx < buffer->read) -> ignore
 */

enum {
	VT_LINE_MAX = 256,
	RUN_MAX = 65536,
};

typedef u64 ringbuffer_idx;

typedef enum {
	RUN_PRINTABLE,
	RUN_ESCAPE,
	RUN_UTF8,
	RUN_KITTY,
} RunType;

typedef struct Run {
	ringbuffer_idx off;
	ringbuffer_idx n;
	RunType type;
} Run;

typedef struct VtLine {
	ringbuffer_idx off;
	ringbuffer_idx n;
	Run *runs;
	u32 run_n;
} VtLine;

/* NOTE(vasco):
 * Our ring buffer struct contains the actual ringbuffer
 * and all the information we gathered via preparsing.
 *
 * This will allow us to parse it incrementally if the user
 * needs scrollback.
 */
typedef struct RingBuffer {
	char *base;
	size_t size; /* multiple of peak_page_size() */
	size_t read;
	size_t write;
	size_t parsed;
	VtLine *line;
	Run *run;
	u32 line_max;
	u32 line_n;
	u32 line_i;
	u32 run_max;
	u32 run_n;
} RingBuffer;

typedef struct {
	u32 line_max;
	u32 run_max;
	size_t pages;
} RingBufferArgs;

extern const char *const ring_buffer_run_name[];

static size_t ringbuffer_size(RingBufferArgs args);
static RingBuffer *ringbuffer_create(RingBufferArgs args, void *memory);
static void ringbuffer_destroy(RingBuffer *b);
static void ringbuffer_produce(RingBuffer *b, size_t n);
static void ringbuffer_consume(RingBuffer *b);
static Run *ringbuffer_runs_from_last_n_lines(RingBuffer *b, u32 n_lines);

/* NOTE(vasco):
 * Least recently used glyth cache.
 *
 * Loosely based on https://github.com/cmuratori/refterm
 *
 * Serves two purposes:
 * 1) Deciding what glyths stay in the atlas.
 *    Since the atlas is limited size but unicode is theoretically massive.
 * 2) Give us a fast path that skips the parser entirely.
 */

/* TODO(vasco):
 *  1) Consider and test some alternate cache designs.
 *  2) Stress-test everything here (WIP)
 */

typedef struct {
  __m128i value;
} GlyphHash;

typedef struct {
  u32 value;
} GlyphIndex;

typedef struct {
  u32 x, y;
} GlyphCachePoint;

enum {
  GLYPH_DIRECT_LO = 32, /* ASCII; '?' is 63 */
  GLYPH_DIRECT_HI = 127,
  GLYPH_DIRECT_ASCII_N = GLYPH_DIRECT_HI - GLYPH_DIRECT_LO + 1,
  GLYPH_DIRECT_FFFD = GLYPH_DIRECT_ASCII_N, /* U+FFFD */
  GLYPH_DIRECT_N = GLYPH_DIRECT_ASCII_N + 1, /* SLOTS 0 .. N-1; LRU STARTS HERE */
};

typedef struct {
  u32 hash_count;
  u32 entry_count;
  u32 reserved_tile_count; // set to VT_Glyph_DIRECT_N
  u32 cache_tile_count;
} GlyphTableParams;

typedef struct {
  u32 id;
  GlyphIndex gpu_idx;
  u32 filled_state;
  u16 dim_x;
  u16 dim_y;
} GlyphState;

typedef struct {
  size_t hit_count;
  size_t miss_count;
  size_t recycle_count;
} GlyphTableStats;

typedef struct {
  GlyphHash hash;
  GlyphIndex gpu_idx;
  u32 filled_state;
  u32 next_hash;
  u32 next_lru;
  u32 prev_lru;
  u16 dim_x;
  u16 dim_y;
} GlyphEntry;

typedef struct {
  GlyphTableStats stats;
  GlyphEntry *entries;
  u32 *hash_table;
  u32 hash_mask;
  u32 hash_count;
  u32 entry_count;
} GlyphTable;

static size_t glyph_table_size(GlyphTableParams params); // bytes for place_in_memory
static GlyphTable *glyph_table_place_in_memory(GlyphTableParams params, void *memory);
static GlyphTableStats glyph_table_stats(GlyphTable *table); // since last call; then zeros
static GlyphState glyph_table_find_hash(GlyphTable *table, GlyphHash hash); // hit or alloc; moves to MRU
static GlyphState glyph_table_peek_hash(GlyphTable *table, GlyphHash hash); // hit only; id 0 if misses; no LRU
static void glyph_table_update_entry(GlyphTable *table, u32 id, u32 new_state, u16 new_dimx, u16 new_dimy); // filled/dims; cache does not raster
static GlyphCachePoint glyph_cache_point_unpack(GlyphIndex idx); // gpu_idx → atlas x,y
static GlyphHash glyph_hash(codepoint_t cp);

enum {
  GLYPH_FILLED = 1,
  GLYPH_FILLED_COLOR = 2,
};

typedef struct {
    unsigned char *atlas;
    uint32_t cell_width;
    uint32_t slot_width;
    uint32_t cell_height;
    uint32_t rows;
    uint32_t cols;
} Atlas;

typedef struct {
  u32 cell_w;
  u32 cell_h;
  float pixel_height;
  float scale;
  int ascent;
  unsigned char *ttf;
  unsigned long ttf_n;
  u8 *scratch;
  stbtt_fontinfo font;
  unsigned char *fallback_ttf;
  unsigned long fallback_ttf_n;
  stbtt_fontinfo fallback_font;
  float fallback_scale;
  int fallback_ascent;
  unsigned char *emoji_ttf;
  unsigned long emoji_ttf_n;
  const unsigned char *emoji_cmap;
  const unsigned char *emoji_cblc;
  const unsigned char *emoji_cbdt;
  unsigned long emoji_cmap_n;
  unsigned long emoji_cblc_n;
  unsigned long emoji_cbdt_n;
} GlyphGenerator;

static int glyph_generator_set_font(GlyphGenerator *g, const char *path, float px);
static int glyph_generator_set_fallback(GlyphGenerator *g, const char *path);
static int glyph_generator_set_emoji(GlyphGenerator *g, const char *path);
static void glyph_generator_destroy(GlyphGenerator *g);
static int glyph_generator_rasterize_codepoint(GlyphGenerator *g, Atlas *atlas, u32 slot, codepoint_t cp);
static void glyph_generator_rasterize_run(GlyphGenerator *g, Atlas *atlas, u32 slot, const codepoint_t *cps, u32 n);

typedef struct VtPush {
    f32 ndc_x;
    f32 ndc_y;
    f32 uv_x;
    f32 uv_y;
    f32 alpha;
} VtPush;

typedef struct VtPushTile {
    u32 cell_w;
    u32 cell_h;
    u32 cols;
    u32 rows;
    f32 uv_x;
    f32 uv_y;
    f32 alpha;
    u32 def_bg;
} VtPushTile;

typedef struct VtInstance {
    u32 pos;
    u32 foreground;
    u32 background;
} VtInstance;

STATIC_ASSERT(sizeof (VtInstance) == 12, "VtInstance is 12 bytes");
STATIC_ASSERT(sizeof (VtPushTile) == 32, "VtPushTile must match vulkan/vt.frag");

typedef struct {
    void *memory;
    Atlas atlas;

    GlyphTable *glyph_table;
    GlyphGenerator generator;

    PeakWindow win;
    RendRenderer gpu;
    RendPipeline pipeline;
    RendBuffer instance;
    u32 instance_cap;
    u32 instance_cols;
    u32 instance_rows;

    RendTexture atlas_tex;

    int tile;
    int atlas_dirty;
    int sel_on;
    u32 sel0;
    u32 sel1;
    u32 mux_cols;
    int drop_hover;
    int drop_dir;
    int drop_first;
    uint32_t current_width;
    uint32_t current_height;
    u32 clear_bg;
} Renderer;

typedef struct {
    u32 glyph_filled;
    u32 glyph_filled_color;
    u32 atlas_cols;
    u32 atlas_rows;
    u32 glyph_n;
    u32 glyph_map_n;
    u32 cell_w;
    u32 cell_h;
    int offscreen;
} RendererParams;

#define GLYPH_COLOR 0x01000000u

static Renderer *renderer_create(RendererParams args);
static void renderer_destroy(Renderer *r);
static int renderer_instance_resize(Renderer *r, u32 cols, u32 rows);
static u32 renderer_fill_cp(Renderer *r, u32 x, u32 y, codepoint_t cp, color_packed_t fg, color_packed_t bg, VtInstance *inst, u32 n, u32 cap);
static u32 renderer_fill_term(Renderer *r, Term *t, u32 ox, u32 oy, VtInstance *inst, u32 n, u32 cap, int cursor);
static int renderer_frame_begin(Renderer *r, u32 bg);
static void renderer_flush(Renderer *r, u32 n);
static void renderer_frame_end(Renderer *r);
static void renderer_screenshot_term_ppm(Renderer *r, Term *term, const char *out_path);

static PeakProc vt_shell_spawn(u32 cols, u32 rows, u32 xpixel, u32 ypixel); // bash --login
static void vt_shell_resize(PeakProc *sh, u32 cols, u32 rows, u32 xpixel, u32 ypixel);
static int vt_shell_read(PeakProc *sh, void *buf, size_t n);
static int vt_shell_write(PeakProc *sh, const void *buf, size_t n);
static int vt_shell_wait(PeakProc *sh, int timeout_ms);
static int vt_shell_reap(PeakProc *sh);
static void vt_shell_close(PeakProc *sh);

#define CTL_CLIENTS 4
#define CTL_LINE 8192
#define CTL_JOB_OUT 65536
#define CTL_READ_N 8
#define CTL_RG_HITS 64
#define CTL_RG_OUT 8192

typedef struct {
	PEAK_HANDLE fd;
	PEAK_HANDLE pass;
	u32 n;
	char buf[CTL_LINE];
} VtCtlClient;

typedef struct {
	const char *id;
	int id_n;
	const char *op;
	int op_n;
	const char *data;
	int data_n;
	const char *cmd;
	int cmd_n;
	const char *path;
	int path_n;
	int y;
	int n;
	int has_y;
	int has_n;
} VtCtlReq;

typedef struct {
	int pid;
	PEAK_HANDLE fd;
	int client;
	int id_n;
	int code;
	u32 seq;
	u32 out_n;
	bool trunc;
	bool dead;
	char id[96];
	char out[CTL_JOB_OUT];
} VtCtlJob;

#define UTF_INVALID TERM_UTF_INVALID
#define RING_PAGES 16

/* NOTE(vasco):
 * Each Pane will have it's own screen and ring buffer.
 *
 * Copying a Pane between terminals should be pretty
 * straight forward since we know the size of everything.
 * Ideally we want one malloc per pane.
 */

enum {
	PANE_MAX = 8,
	NODE_MAX = 15,
	SPLIT_LEAF = 0,
	SPLIT_H = 1,
	SPLIT_V = 2,
	MUX_WALL_MAX = 512 * 256,
	MUX_ARM_N = 1,
	MUX_ARM_E = 2,
	MUX_ARM_S = 4,
	MUX_ARM_W = 8
};

typedef struct Kitty {
	char *b64;
	u32 b64_n;
	u32 b64_cap;
	u32 id;
	u32 cols;
	u32 rows;
	int action;
	int no_cursor;
} Kitty;

typedef struct Pane {
	Term term;
	RingBuffer *rb;
	PeakProc sh;
	Kitty kitty;

	u32 x;
	u32 y;
	u32 cols;
	u32 rows;
	int used;
} Pane;

typedef struct Node {
	u8 split;
	u16 a;
	u16 b;
	u16 pane;
	u16 ratio;
} Node;

static const codepoint_t mux_box[16] = {
	0,
	0x2502, 0x2500, 0x2514,
	0x2502, 0x2502, 0x250C, 0x251C,
	0x2500, 0x2518, 0x2500, 0x2534,
	0x2510, 0x2524, 0x252C, 0x253C
};

typedef struct {
} MultiplexorArgs;

/* NOTE(vasco):
 * VT is basically a file descriptor pty controller
 */
typedef struct {
    Pane panes[PANE_MAX];
    Node mux_nodes[NODE_MAX];
    u8 node_used[NODE_MAX];
    u8 wall[MUX_WALL_MAX];

    TermColors mux_colors; // hmm

    u32 focus;
    Pane *pane;
    u32 cols;
    u32 rows;
    int prefix;
    int drag;
    int os_src;
    int hover;
    int drop_dir;
    int drop_first;
    int click_paste;
} Multiplexor;

typedef Pane VtPane;
typedef Multiplexor VtMultiplexor;
typedef Kitty VtKitty;
typedef Node VtNode;

#define VT_PANE_MAX PANE_MAX
#define VT_NODE_MAX NODE_MAX
#define VT_SPLIT_LEAF SPLIT_LEAF
#define VT_SPLIT_H SPLIT_H
#define VT_SPLIT_V SPLIT_V
#define VT_MUX_WALL_MAX MUX_WALL_MAX
#define VT_MUX_ARM_N MUX_ARM_N
#define VT_MUX_ARM_E MUX_ARM_E
#define VT_MUX_ARM_S MUX_ARM_S
#define VT_MUX_ARM_W MUX_ARM_W
#define VT_RING_PAGES RING_PAGES

static void mux_bind(Multiplexor *m, u32 i);
static void mux_reset(Multiplexor *m);
static int mux_open(Multiplexor *m, u32 i, u32 cols, u32 rows, const TermColors *colors, Renderer *renderer);
static void mux_close(Multiplexor *m, u32 i);
static void mux_destroy(Multiplexor *m);
static u16 mux_node_alloc(Multiplexor *m);
static void mux_node_free(Multiplexor *m, u16 i);
static u16 mux_leaf_of(Multiplexor *m, u32 pane);
static u16 mux_parent_of(Multiplexor *m, u16 ni);
static void mux_layout_node(Multiplexor *m, u16 ni, u32 x, u32 y, u32 cols, u32 rows, Renderer *renderer);
static void mux_frame(Multiplexor *m);
static void mux_stitch(Multiplexor *m);
static u32 mux_used(Multiplexor *m);
static void mux_layout(Multiplexor *m, u32 cols, u32 rows, Renderer *renderer);
static u32 mux_first(Multiplexor *m);
static void mux_focus(Multiplexor *m, u32 i);
static void mux_focus_next(Multiplexor *m);
static void mux_focus_dir(Multiplexor *m, int dx, int dy);
static int mux_split(Multiplexor *m, int dir, Renderer *renderer);
static void mux_attach_side(Multiplexor *m, int *dir, int *first);
static int mux_attach(Multiplexor *m, PeakProc proc, int dir, int first, Renderer *renderer);
static void mux_handoff(Multiplexor *m, u32 i, Renderer *renderer);
static PEAK_HANDLE mux_connect_pid(int pid);
static int mux_sock_line(PEAK_HANDLE fd, char *dst, size_t cap);
static int mux_export(Multiplexor *m, u32 src, int pid, Renderer *renderer);
static int mux_pull(Multiplexor *m, const char *path, int pane, Renderer *renderer);
static int mux_find_hit(void);
static int mux_offer_path(char *dst, size_t cap);
static void mux_offer_write(u32 pane);
static void mux_offer_clear(void);
static int mux_offer_take(Multiplexor *m, Renderer *renderer);
static void mux_collapse(Multiplexor *m, u16 leaf);
static int mux_move(Multiplexor *m, u32 src, u32 dst, int dir, int first, Renderer *renderer);
static void mux_kill_pane(Multiplexor *m, u32 i, Renderer *renderer);
static u32 mux_fds(Multiplexor *m, PEAK_HANDLE *fds);
static u32 mux_fill_walls(Multiplexor *m, Renderer *r, VtInstance *inst, u32 n, u32 cap);
static u32 mux_fill_drop(Multiplexor *m, Renderer *r, VtInstance *inst, u32 n, u32 cap);
static void mux_resize(Multiplexor *m, u32 cols, u32 rows, Renderer *renderer);
static int mux_pick(Multiplexor *m, u32 x, u32 y, u32 *lx, u32 *ly);
static int mux_pick_near(Multiplexor *m, u32 x, u32 y, u32 *lx, u32 *ly);
static void mux_drag_side(Multiplexor *m, u32 i, u32 x, u32 y, int *dir, int *first);
static void mux_drag_over(Multiplexor *m, u32 x, u32 y);
static void mux_os_drag_start(Multiplexor *m);
static int mux_drop_self(Multiplexor *m, Renderer *renderer);
static int mux_pointer(Multiplexor *m, u32 x, u32 y, PeakPointerState st, PeakKeyMod mod, Renderer *renderer);
static int mux_ch_hit(const char *s, PeakKeyCode key, u32 ch);
static int mux_chord(PeakKeyCode want, PeakKeyMod want_mod, PeakKeyCode key, PeakKeyMod mod);
static int mux_key(Multiplexor *m, PeakKeyCode key, PeakKeyMod mod, u32 code, Renderer *renderer);

int osc52(const char *p, u32 n);
