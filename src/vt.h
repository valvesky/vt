#pragma once
#define VT_MAJOR 0
#define VT_MINOR 2
#define VT_PATCH 1

/* CHANGE LOG
 * 0.1.0 - @vasco - Peak Rend Term; ctl; headless
 * 0.1.1 - @vasco - SGR/X10 wheel when child enables mouse
 * 0.1.2 - @vasco - win32 headless compile
 * 0.1.3 - @vasco - SSE ascii runs; draw from TermCell.is_dirty
 * 0.2.0 - @vasco - ring+prepass; TermCell SSBO; slang BDA present; atlas LRU
 * 0.2.1 - @vasco - cellMain dest blit; glyph get on UTF-8 ingest
 */
#include "term.h"

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
#define DIVCEIL(n, d)    (((n) + ((d) - 1)) / (d))
#define DEFAULT(a, b)    (a) = (a) ? (a) : (b)
#define LIMIT(x, a, b)   (x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x)

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
STATIC_ASSERT(sizeof (TermCell) == 16, "TermCell must be 16 bytes");

typedef struct Renderer Renderer;

enum {
  /* Pages in the ingest ring. Must fit one PTY read (16KiB) plus an
   * incomplete sequence left unread. 16 * 4KiB = 64KiB on typical Linux. */
  VT_RING_PAGES = 16,
  /* Max classified runs from one prepass call. A 64KiB ASCII dump is
   * one run. A worst-case byte soup is one PARSE atom per byte; the
   * caller loops until the ring head is incomplete or empty. */
  VT_RUN_MAX = 256,
  VT_ATLAS_COLS = 30,
  VT_ATLAS_ROWS = 30,
  VT_GLYPH_N = 30 * 30, /* atlas slots = LRU capacity */
  VT_GLYPH_MAP_N = 2048, /* GPU/CPU hash buckets; 0 in cp = empty */
  VT_GLYPH_PIN_LO = 32,
  VT_GLYPH_PIN_HI = 127,
};

#define VT_LRU_NONE 0xffffffffu

STATIC_ASSERT(VT_GLYPH_N == VT_ATLAS_COLS * VT_ATLAS_ROWS, "LRU cap is atlas slots");

/* GPU push. Same layout as PushConstants in vulkan/vt.slang. */
typedef struct VtPush {
  u64 cells;
  u64 glyph_cp;
  u64 glyph_slot;
  u64 atlas;
  u32 cols;
  u32 rows;
  u32 cell_w;
  u32 cell_h;
  u32 atlas_w;
  u32 atlas_h;
  u32 cursor_x;
  u32 cursor_y;
  u32 cursor_on;
  u32 clear_bg;
  f32 alpha;
  u32 pad;
  u64 dest;
  u32 fb_w;
  u32 fb_h;
} VtPush;

/* Page-mirrored ingest ring. Not scrollback. Unread is always linear:
 * vt_ring_head()[0 .. vt_ring_unread()). The second map at base+size
 * makes a wrap look like a contiguous memcpy destination / scan. */
typedef struct VtRing {
  char *base;
  size_t size; /* multiple of peak_page_size() */
  size_t r;    /* consumed byte count */
  size_t w;    /* produced byte count; unread = w - r, 0..size */
} VtRing;

/* Classify-only. ASCII is the SIMD fast path into term_feed_ascii.
 * PARSE is one atom for term_feed: C0, one ESC family sequence, or
 * one UTF-8 scalar. Prepass never calls Term. */
typedef enum VtRunKind {
  VT_RUN_ASCII = 0,
  VT_RUN_PARSE = 1,
} VtRunKind;

typedef struct VtRun {
  u32 off; /* bytes from the ring head passed to vt_prepass */
  u32 n;
  VtRunKind kind;
} VtRun;

/* Map N pages (N * peak_page_size()) as two adjacent views of the same
 * memfd. size 0 / NULL base on failure. */
bool vt_ring_init(VtRing *ring, size_t pages);
void vt_ring_destroy(VtRing *ring);

/* w - r. Always <= size. */
size_t vt_ring_unread(const VtRing *ring);

/* Linear write cursor: base + (w % size). Room is size - unread.
 * Caller writes into this span, then vt_ring_produce. */
char *vt_ring_tail(VtRing *ring);
size_t vt_ring_room(const VtRing *ring);

/* Advance w by n. False and no change if n > room (never overwrite
 * unread). */
bool vt_ring_produce(VtRing *ring, size_t n);

/* Linear read cursor: base + (r % size), length unread. */
const char *vt_ring_head(const VtRing *ring);

/* Advance r by n. n must be <= unread. */
void vt_ring_consume(VtRing *ring, size_t n);

/* Scan [data, data+n). Emit complete runs into out[0..cap).
 * A trailing incomplete ESC/UTF-8 is not emitted; the caller leaves
 * those bytes in the ring. Returns the number of runs. Bytes covered
 * are out[0].off .. out[n-1].off+out[n-1].n, which starts at 0.
 * PARSE atom length is what term_feed consumes: C0; CSI/OSC/DCS to
 * terminator; ESC + 0x20..0x2F + one more (ESC ( B, ESC ) , ESC #,
 * ESC %); other ESC is 2 bytes; one UTF-8 scalar.
 * term_state / utf8_rem are only for the force-feed-full case: if Term
 * is already mid-sequence, the first run is PARSE. The normal ingest
 * path passes 0, 0. */
u32 vt_prepass(const char *data, u32 n, u32 term_state, u8 utf8_rem,
    VtRun *out, u32 cap);

/* term_feed_ascii for VT_RUN_ASCII, term_feed for VT_RUN_PARSE.
 * base is the pointer passed to vt_prepass (the ring head). */
void vt_feed_runs(Term *t, const char *base, const VtRun *runs, u32 n);

/* Packed atlas coordinate. Slot 0 is space. Never a missing-sentinel:
 * a failed get returns the replacement slot (U+FFFD), not 0. */
typedef u32 vt_glyph_id;

/* Hashmap + doubly linked list over VT_GLYPH_N atlas slots.
 * cp[] / slot[] is the GPU hash (open address, 2048, 0 empty).
 * slot_cp / pin / prev / next / mru / lru / used are CPU only. */
typedef struct VtLRU {
  codepoint_t cp[VT_GLYPH_MAP_N];
  vt_glyph_id slot[VT_GLYPH_MAP_N];
  codepoint_t slot_cp[VT_GLYPH_N];
  u32 prev[VT_GLYPH_N];
  u32 next[VT_GLYPH_N];
  u8 pin[VT_GLYPH_N];
  u32 mru;
  u32 lru;
  u32 used;
} VtLRU;

void vt_lru_init(VtLRU *l);
/* Atlas slot index, or VT_LRU_NONE. */
u32 vt_lru_find(const VtLRU *l, codepoint_t cp);
void vt_lru_touch(VtLRU *l, u32 slot);
/* Free slot, or evicted unpinned tail, or VT_LRU_NONE. */
u32 vt_lru_alloc(VtLRU *l);
void vt_lru_put(VtLRU *l, codepoint_t cp, u32 slot, int pin);
/* Hash only: cp -> pack(slot). For font-miss -> U+FFFD. */
void vt_lru_alias(VtLRU *l, codepoint_t cp, u32 slot);
vt_glyph_id vt_lru_pack(u32 slot);

/* Touch cp. Hit: move-to-front, return packed slot. Miss: rasterize
 * into a free slot, or evict LRU unpinned tail then rasterize, then
 * insert at MRU. Font-miss (no glyph index) inserts cp -> U+FFFD's
 * slot so the next lookup hits; it does not FindGlyphIndex again.
 * Pins VT_GLYPH_PIN_LO..HI and U+FFFD (never eviction victims).
 * If used==VT_GLYPH_N and every slot is pinned, return U+FFFD.
 * Updates the GPU glyph-map SSBO when the windowed renderer exists. */
vt_glyph_id vt_glyph_get(codepoint_t cp);

/* vt_ingest / vt_resize / vt_present stay static in vt.c.
 * ingest: ring produce, prepass, feed_runs, consume. Never drop unread.
 * resize: new screen/alt SSBOs, term_resize_on onto those maps.
 * present: live screen or alt BDA. cellMain dest blit. No hist.
 * wheel: mouse SGR/X10 64/65, else alt CSI A/B, else ignore. */
