#pragma once
#define VT_MAJOR 0
#define VT_MINOR 5
#define VT_PATCH 0

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
STATIC_ASSERT(sizeof (TermCell) == 8, "TermCell must be 8 bytes"); // looking at you ghostty

typedef struct Renderer Renderer;

enum {
    // The ring buffer must be a multiple of some
    VT_RING_PAGES = 16,
    // runs are structs created by the preparser that separate complex sequences to parse
    // from simple ones, saving the real parser some work
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
  f32 ndc_x;
  f32 ndc_y;
  f32 uv_x;
  f32 uv_y;
  f32 alpha;
} VtPush;

/* One instance per drawn cell. Vertex shader expands to a quad.
 * pos: x16 y16. fg: R8G8B8 | col6<<2 | underline/struck. bg: R8G8B8 | row8. */
typedef struct VtInstance {
  u32 pos;
  u32 foreground;
  u32 background;
} VtInstance;

STATIC_ASSERT(sizeof (VtInstance) == 12, "VtInstance is 12 bytes");
STATIC_ASSERT(VT_ATLAS_COLS <= 63, "glyph col lives in 6 bits");
STATIC_ASSERT(VT_ATLAS_ROWS <= 255, "glyph row lives in 8 bits");

/**
 * Page-mirrored circular buffer. Not scrollback!!!
 * The mirroring means we can read size at any point
 * in time. Even at the last byte of the buffer, 
 * thus making memcpy always valid without checking anything.
 * THANK YOU OS DEVELOPERS!
 */
typedef struct VtRing {
  char *base;
  size_t size; /* multiple of peak_page_size() */
  size_t r;    /* consumed byte count */
  size_t w;    /* produced byte count; unread = w - r, 0..size */
} VtRing;

/**
 * A run is a preparsed sequence of bytes ready to
 * be fed to the terminal emulator to produce the final screen.
 * type picks the Term feed: printable, escape, or utf8.
 */
enum VtRunType {
    VT_RUN_PRINTABLE,
    VT_RUN_ESCAPE,
    VT_RUN_UTF8,
};
typedef struct VtRun {
  u32 off;
  u32 n;
  int type;
} VtRun;

typedef u32 vt_glyph_id;

/**
 * Least. Recently. Used.
 * Since the atlas is limited size but unicode is
 * theoretically massive, we must pick what glyths
 * to keep on the atlas.
 */
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

bool vt_ring_init(VtRing *ring, size_t pages);
void vt_ring_destroy(VtRing *ring);
char *vt_ring_tail(VtRing *ring);
size_t vt_ring_room(const VtRing *ring);
bool vt_ring_produce(VtRing *ring, size_t n);
const char *vt_ring_head(const VtRing *ring);
void vt_ring_consume(VtRing *ring, size_t n);

void vt_lru_init(VtLRU *l);
vt_glyph_id vt_lru_peek(const VtLRU *l, codepoint_t cp);
u32 vt_lru_find(const VtLRU *l, codepoint_t cp);
void vt_lru_touch(VtLRU *l, u32 slot);
u32 vt_lru_alloc(VtLRU *l);
void vt_lru_put(VtLRU *l, codepoint_t cp, u32 slot, int pin);
void vt_lru_alias(VtLRU *l, codepoint_t cp, u32 slot);
vt_glyph_id vt_lru_pack(u32 slot);

vt_glyph_id vt_glyph_get(codepoint_t cp);
