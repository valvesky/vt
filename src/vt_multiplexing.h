#ifndef VT_MULTIPLEXING_H
#define VT_MULTIPLEXING_H

#define VT_MAJOR 0
#define VT_MINOR 7
#define VT_PATCH 10

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
 * 0.7.8 - @vasco - vt_ring_buffer: ring + lines + 64-bit run indexes
 * 0.7.9 - @vasco - ring keeps tail; last N lines at present, not every read
 * 0.7.10 - @vasco - drop hz; present when PTY idle
 */

#include "vt_term.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifndef MIN
#define MIN(a, b)        ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b)        ((a) > (b) ? (a) : (b))
#endif
#define LEN(a)           (sizeof(a) / sizeof(a)[0])
#define BETWEEN(x, a, b) ( ((unsigned)((x) - (a))) <= (unsigned)((b) - (a)) )

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

#if defined(__clang__) || defined(__GNUC__)
#define STATIC_ASSERT _Static_assert
#else
#define STATIC_ASSERT static_assert
#endif

#pragma GCC poison wchar_t
typedef u32 codepoint_t;
typedef u32 color_packed_t;

#define UTF_INVALID TERM_UTF_INVALID

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
STATIC_ASSERT(sizeof (TermCell) == 12, "TermCell is codepoint+style+tag+glyph");

#include "vt_ring_buffer.h"

enum {
    // The ring buffer must be a multiple of some power of two
    VT_RING_PAGES = 16,
};

#include "vt_renderer.h"

/* NOTE(vasco):
 * Each Pane will have it's own screen and ring buffer.
 *
 * Copying a Pane between terminals should be pretty
 * straight forward since we know the size of everything.
 * Ideally we want one malloc per pane.
 */

enum {
	VT_PANE_MAX = 8,
	VT_NODE_MAX = 15,
	VT_SPLIT_LEAF = 0,
	VT_SPLIT_H = 1,
	VT_SPLIT_V = 2,
	VT_MUX_WALL_MAX = 512 * 256,
	VT_MUX_ARM_N = 1,
	VT_MUX_ARM_E = 2,
	VT_MUX_ARM_S = 4,
	VT_MUX_ARM_W = 8
};

typedef struct VtKitty {
	char *b64;
	u32 b64_n;
	u32 b64_cap;
	u32 id;
	u32 cols;
	u32 rows;
	int action;
	int no_cursor;
} VtKitty;

typedef struct VtPane {
	Term term;
	VtRingBuffer *rb;
	PeakProc sh;
	VtKitty kitty;
	u32 x;
	u32 y;
	u32 cols;
	u32 rows;
	int used;
} VtPane;

typedef struct VtNode {
	u8 split;
	u16 a;
	u16 b;
	u16 pane;
	u16 ratio;
} VtNode;

static const codepoint_t vt_mux_box[16] = {
	0,
	0x2502, 0x2500, 0x2514,
	0x2502, 0x2502, 0x250C, 0x251C,
	0x2500, 0x2518, 0x2500, 0x2534,
	0x2510, 0x2524, 0x252C, 0x253C
};

typedef struct {
} VtMultiplexorArgs;

/* NOTE(vasco):
 * VT is basically a file descriptor pty controller
 */
typedef struct {
    VtPane panes[VT_PANE_MAX];
    VtNode mux_nodes[VT_NODE_MAX];
    u8 node_used[VT_NODE_MAX];
    u8 wall[VT_MUX_WALL_MAX];

    TermColors mux_colors; // hmm

    u32 focus;
    VtPane *vt_pane;
    u32 cols;
    u32 rows;
    int prefix;
    int drag;
    int os_src;
    int hover;
    int drop_dir;
    int drop_first;
    int click_paste;
} VtMultiplexor;

static void     vt_mux_bind(VtMultiplexor *m, u32 i);
static void     vt_mux_reset(VtMultiplexor *m);
static int      vt_mux_open(VtMultiplexor *m, u32 i, u32 cols, u32 rows, const TermColors *colors);
static void     vt_mux_close(VtMultiplexor *m, u32 i);
static void     vt_mux_destroy(VtMultiplexor *m);
static u16      vt_mux_node_alloc(VtMultiplexor *m);
static void     vt_mux_node_free(VtMultiplexor *m, u16 i);
static u16      vt_mux_leaf_of(VtMultiplexor *m, u32 pane);
static u16      vt_mux_parent_of(VtMultiplexor *m, u16 ni);
static void     vt_mux_layout_node(VtMultiplexor *m, u16 ni, u32 x, u32 y, u32 cols, u32 rows);
static void     vt_mux_layout(VtMultiplexor *m, u32 cols, u32 rows);
static u32      vt_mux_first(VtMultiplexor *m);
static void     vt_mux_focus(VtMultiplexor *m, u32 i);
static void     vt_mux_focus_next(VtMultiplexor *m);
static void     vt_mux_focus_dir(VtMultiplexor *m, int dx, int dy);
static int      vt_mux_split(VtMultiplexor *m, int dir);
static void     vt_mux_attach_side(VtMultiplexor *m, int *dir, int *first);
static int      vt_mux_attach(VtMultiplexor *m, PeakProc proc, int dir, int first);
static void     vt_mux_handoff(VtMultiplexor *m, u32 i);
static PEAK_HANDLE vt_mux_connect_pid(int pid);
static int      vt_mux_sock_line(PEAK_HANDLE fd, char *dst, size_t cap);
static int      vt_mux_export(VtMultiplexor *m, u32 src, int pid);
static int      vt_mux_pull(VtMultiplexor *m, const char *path, int pane);
static int      vt_mux_find_hit(void);
static int      vt_mux_offer_path(char *dst, size_t cap);
static void     vt_mux_offer_write(u32 pane);
static void     vt_mux_offer_clear(void);
static int      vt_mux_offer_take(VtMultiplexor *m);
static void     vt_mux_collapse(VtMultiplexor *m, u16 leaf);
static int      vt_mux_move(VtMultiplexor *m, u32 src, u32 dst, int dir, int first);
static void     vt_mux_kill(VtMultiplexor *m, u32 i);
static u32      vt_mux_fds(VtMultiplexor *m, PEAK_HANDLE *fds);
static void     vt_mux_resize(VtMultiplexor *m, u32 cols, u32 rows);
static int      vt_mux_pick(VtMultiplexor *m, u32 x, u32 y, u32 *lx, u32 *ly);
static void     vt_mux_drag_side(VtMultiplexor *m, u32 i, u32 x, u32 y, int *dir, int *first);
static void     vt_mux_drag_over(VtMultiplexor *m, u32 x, u32 y);
static void     vt_mux_os_drag_start(VtMultiplexor *m);
static int      vt_mux_drop_self(VtMultiplexor *m);
static int      vt_mux_pointer(VtMultiplexor *m, u32 x, u32 y, PeakPointerState st, PeakKeyMod mod);
static int      vt_mux_ch_hit(const char *s, PeakKeyCode key, u32 ch);
static int      vt_mux_chord(PeakKeyCode want, PeakKeyMod want_mod, PeakKeyCode key, PeakKeyMod mod);
static int      vt_mux_key(VtMultiplexor *m, PeakKeyCode key, PeakKeyMod mod, u32 code);
static u32      vt_mux_fill_walls(VtMultiplexor *m, VtInstance *inst, u32 n, u32 cap);
static u32      vt_mux_fill_drop(VtMultiplexor *m, VtInstance *inst, u32 n, u32 cap);
static void     vt_mux_present(VtMultiplexor *m);

#endif
