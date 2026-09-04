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

#ifndef VT_GLYTH_CACHE_H
#define VT_GLYTH_CACHE_H

#include <immintrin.h>
#include <stddef.h>

typedef struct {
  __m128i value;
} VtGlythHash;

typedef struct {
  u32 value;
} VtGlythIndex;

typedef struct {
  u32 x, y;
} VtGlythCachePoint;

enum {
  VT_GLYTH_DIRECT_LO = 32, /* ASCII; '?' is 63 */
  VT_GLYTH_DIRECT_HI = 127,
  VT_GLYTH_DIRECT_ASCII_N = VT_GLYTH_DIRECT_HI - VT_GLYTH_DIRECT_LO + 1,
  VT_GLYTH_DIRECT_FFFD = VT_GLYTH_DIRECT_ASCII_N, /* U+FFFD */
  VT_GLYTH_DIRECT_N = VT_GLYTH_DIRECT_ASCII_N + 1, /* slots 0 .. N-1; LRU starts here */
};

typedef struct {
  u32 hash_count;
  u32 entry_count;
  u32 reserved_tile_count; // set to VT_GLYTH_DIRECT_N
  u32 cache_tile_count;
} VtGlythTableParams;

typedef struct {
  u32 id;
  VtGlythIndex gpu_idx;
  u32 filled_state;
  u16 dim_x;
  u16 dim_y;
} VtGlythState;

typedef struct {
  size_t hit_count;
  size_t miss_count;
  size_t recycle_count;
} VtGlythTableStats;

typedef struct {
  VtGlythHash hash;
  u32 next_hash;
  u32 next_lru;
  u32 prev_lru;
  VtGlythIndex gpu_idx;
  u32 filled_state;
  u16 dim_x;
  u16 dim_y;
} VtGlythEntry;

typedef struct {
  VtGlythTableStats stats;
  u32 hash_mask;
  u32 hash_count;
  u32 entry_count;
  u32 *hash_table;
  VtGlythEntry *entries;
} VtGlythTable;

/* USAGE:
 * VtGlythTableParams params = { ... };
 * void *mem = malloc(vt_glyth_table_size(params));
 * VtGlythTable *table = vt_glyth_table_place_in_memory(params, mem);
 * if (table) {
 *     // ASCII slot = cp - VT_GLYTH_DIRECT_LO; U+FFFD = VT_GLYTH_DIRECT_FFFD
 * }
 */
static size_t vt_glyth_table_size(VtGlythTableParams params); // bytes for place_in_memory
static VtGlythTable *vt_glyth_table_place_in_memory(VtGlythTableParams params, void *memory); // NULL in → NULL out
static VtGlythTableStats vt_glyth_table_stats(VtGlythTable *table); // since last call; then zeros
static VtGlythState vt_glyth_table_find_hash(VtGlythTable *table, VtGlythHash hash); // hit or alloc; moves to MRU
static VtGlythState vt_glyth_table_peek_hash(VtGlythTable *table, VtGlythHash hash); // hit only; id 0 if miss; no LRU
static void vt_glyth_table_update_entry(VtGlythTable *table, u32 id, u32 new_state, u16 new_dimx, u16 new_dimy); // filled/dims; cache does not raster
static VtGlythCachePoint vt_glyth_cache_point_unpack(VtGlythIndex idx); // gpu_idx → atlas x,y
static VtGlythHash vt_glyth_hash(const u8 *utf8, size_t n); // UTF-8 bytes, 128-bit

#endif
