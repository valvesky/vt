#pragma once

#include "vt_glyth_cache.h"

#include <assert.h>
#include <string.h>

static __m128i       vt_internal_glyth_hash_round(__m128i h, __m128i in);
static int           vt_internal_glyth_hash_eq(VtGlythHash a, VtGlythHash b);
static u32          *vt_internal_glyth_slot_pointer(VtGlythTable *table, VtGlythHash hash);
static VtGlythEntry *vt_internal_glyth_entry(VtGlythTable *table, u32 index);
static VtGlythEntry *vt_internal_glyth_sentinel(VtGlythTable *table);
static void          vt_internal_glyth_recycle(VtGlythTable *table);
static u32           vt_internal_glyth_pop_free(VtGlythTable *table);

__m128i
vt_internal_glyth_hash_round(__m128i h, __m128i in)
{
  h = _mm_xor_si128(h, in);
#ifdef __AES__
  h = _mm_aesdec_si128(h, _mm_setzero_si128());
  h = _mm_aesdec_si128(h, _mm_setzero_si128());
  h = _mm_aesdec_si128(h, _mm_setzero_si128());
  h = _mm_aesdec_si128(h, _mm_setzero_si128());
#else
  h = _mm_xor_si128(h, _mm_shuffle_epi32(h, 0x4E));
  h = _mm_add_epi64(h, _mm_shuffle_epi32(in, 0x1B));
  h = _mm_xor_si128(h, _mm_slli_epi32(h, 5));
  h = _mm_xor_si128(h, _mm_srli_epi32(h, 7));
#endif
  return h;
}

int
vt_internal_glyth_hash_eq(VtGlythHash a, VtGlythHash b)
{
  __m128i compare;

  compare = _mm_cmpeq_epi32(a.value, b.value);
  return _mm_movemask_epi8(compare) == 0xffff;
}

u32 *
vt_internal_glyth_slot_pointer(VtGlythTable *table, VtGlythHash hash)
{
  u32 hash_index;
  u32 hash_slot;

  hash_index = (u32)_mm_cvtsi128_si32(hash.value);
  hash_slot = hash_index & table->hash_mask;
  assert(hash_slot < table->hash_count);
  return &table->hash_table[hash_slot];
}

VtGlythEntry *
vt_internal_glyth_entry(VtGlythTable *table, u32 index)
{
  assert(index < table->entry_count);
  return table->entries + index;
}

VtGlythEntry *
vt_internal_glyth_sentinel(VtGlythTable *table)
{
  return table->entries;
}

void
vt_internal_glyth_recycle(VtGlythTable *table)
{
  VtGlythEntry *sentinel;
  VtGlythEntry *entry;
  VtGlythEntry *prev;
  u32 *next_index;
  u32 entry_index;

  /* NOTE(vasco): no unused entries left; evict least recently used */
  sentinel = vt_internal_glyth_sentinel(table);
  assert(sentinel->prev_lru);
  entry_index = sentinel->prev_lru;
  entry = vt_internal_glyth_entry(table, entry_index);
  prev = vt_internal_glyth_entry(table, entry->prev_lru);
  prev->next_lru = 0;
  sentinel->prev_lru = entry->prev_lru;

  next_index = vt_internal_glyth_slot_pointer(table, entry->hash);
  while (*next_index != entry_index) {
    assert(*next_index);
    next_index = &vt_internal_glyth_entry(table, *next_index)->next_hash;
  }
  assert(*next_index == entry_index);
  *next_index = entry->next_hash;
  entry->next_hash = sentinel->next_hash;
  sentinel->next_hash = entry_index;

  vt_glyth_table_update_entry(table, entry_index, 0, 0, 0);
  table->stats.recycle_count++;
}

u32
vt_internal_glyth_pop_free(VtGlythTable *table)
{
  VtGlythEntry *sentinel;
  VtGlythEntry *entry;
  u32 result;

  sentinel = vt_internal_glyth_sentinel(table);
  if (!sentinel->next_hash)
    vt_internal_glyth_recycle(table);

  result = sentinel->next_hash;
  assert(result);
  entry = vt_internal_glyth_entry(table, result);
  sentinel->next_hash = entry->next_hash;
  entry->next_hash = 0;

  assert(entry != sentinel);
  assert(entry->dim_x == 0);
  assert(entry->dim_y == 0);
  assert(entry->filled_state == 0);
  assert(entry->next_hash == 0);
  return result;
}

VtGlythHash
vt_glyth_hash(const u8 *utf8, size_t n)
{
  /* NOTE(vasco): UTF-8 bytes. 16-byte chunks, 128-bit AES/fallback state. */
  static const u8 seed[16] = {
    178, 201, 95, 240, 40, 41, 143, 216,
    2, 209, 178, 114, 232, 4, 176, 188
  };
  const u8 *at;
  VtGlythHash result;
  __m128i h;
  __m128i in;
  size_t chunks;
  size_t over;
  u8 tmp[16];

  at = utf8;
  h = _mm_cvtsi64_si128((long long)n);
  h = _mm_xor_si128(h, _mm_loadu_si128((const __m128i *)seed));
  chunks = n / 16;
  while (chunks--) {
    in = _mm_loadu_si128((const __m128i *)at);
    at += 16;
    h = vt_internal_glyth_hash_round(h, in);
  }
  over = n % 16;
  memset(tmp, 0, sizeof tmp);
  if (over && at)
    memcpy(tmp, at, over);
  in = _mm_loadu_si128((const __m128i *)tmp);
  h = vt_internal_glyth_hash_round(h, in);
  result.value = h;
  return result;
}

size_t
vt_glyth_table_size(VtGlythTableParams params)
{
  size_t hash_size;
  size_t entry_size;

  hash_size = params.hash_count * sizeof (u32);
  entry_size = params.entry_count * sizeof (VtGlythEntry);
  return sizeof (VtGlythTable) + hash_size + entry_size;
}

VtGlythTable *
vt_glyth_table_place_in_memory(VtGlythTableParams params, void *memory)
{
  VtGlythTable *result;
  VtGlythEntry *entries;
  u32 starting_tile;
  u32 x;
  u32 y;
  u32 entry_index;

  result = 0;
  if (!memory)
    return result;

  assert(params.hash_count >= 1);
  assert(params.entry_count >= 2);
  assert(params.hash_count && (params.hash_count & (params.hash_count - 1)) == 0);
  assert(params.cache_tile_count >= 1);

  /* NOTE(vasco): entries first so __m128i hash loads stay aligned */
  entries = (VtGlythEntry *)memory;
  result = (VtGlythTable *)(entries + params.entry_count);
  result->hash_table = (u32 *)(result + 1);
  result->entries = entries;
  result->hash_mask = params.hash_count - 1;
  result->hash_count = params.hash_count;
  result->entry_count = params.entry_count;
  memset(result->hash_table, 0, result->hash_count * sizeof *result->hash_table);
  memset(entries, 0, params.entry_count * sizeof (VtGlythEntry));

  starting_tile = params.reserved_tile_count;
  x = starting_tile % params.cache_tile_count;
  y = starting_tile / params.cache_tile_count;
  for (entry_index = 0; entry_index < params.entry_count; entry_index++) {
    VtGlythEntry *entry;

    if (x >= params.cache_tile_count) {
      x = 0;
      y++;
    }
    entry = vt_internal_glyth_entry(result, entry_index);
    if (entry_index + 1 < params.entry_count)
      entry->next_hash = entry_index + 1;
    else
      entry->next_hash = 0;
    entry->gpu_idx.value = (x << 16) | y;
    x++;
  }
  vt_glyth_table_stats(result);
  return result;
}

VtGlythTableStats
vt_glyth_table_stats(VtGlythTable *table)
{
  VtGlythTableStats result;

  result = table->stats;
  memset(&table->stats, 0, sizeof table->stats);
  return result;
}

VtGlythState
vt_glyth_table_peek_hash(VtGlythTable *table, VtGlythHash hash)
{
  u32 *slot;
  u32 entry_index;
  VtGlythState state;

  memset(&state, 0, sizeof state);
  slot = vt_internal_glyth_slot_pointer(table, hash);
  entry_index = *slot;
  while (entry_index) {
    VtGlythEntry *entry;

    entry = vt_internal_glyth_entry(table, entry_index);
    if (vt_internal_glyth_hash_eq(entry->hash, hash)) {
      state.id = entry_index;
      state.dim_x = entry->dim_x;
      state.dim_y = entry->dim_y;
      state.gpu_idx = entry->gpu_idx;
      state.filled_state = entry->filled_state;
      return state;
    }
    entry_index = entry->next_hash;
  }
  return state;
}

VtGlythState
vt_glyth_table_find_hash(VtGlythTable *table, VtGlythHash hash)
{
  VtGlythEntry *result;
  VtGlythEntry *sentinel;
  VtGlythEntry *next_lru;
  VtGlythEntry *prev;
  VtGlythEntry *next;
  u32 *slot;
  u32 entry_index;
  VtGlythState state;

  result = 0;
  slot = vt_internal_glyth_slot_pointer(table, hash);
  entry_index = *slot;
  while (entry_index) {
    VtGlythEntry *entry;

    entry = vt_internal_glyth_entry(table, entry_index);
    if (vt_internal_glyth_hash_eq(entry->hash, hash)) {
      result = entry;
      break;
    }
    entry_index = entry->next_hash;
  }

  if (result) {
    assert(entry_index);
    prev = vt_internal_glyth_entry(table, result->prev_lru);
    next = vt_internal_glyth_entry(table, result->next_lru);
    prev->next_lru = result->next_lru;
    next->prev_lru = result->prev_lru;
    table->stats.hit_count++;
  } else {
    entry_index = vt_internal_glyth_pop_free(table);
    assert(entry_index);
    result = vt_internal_glyth_entry(table, entry_index);
    assert(result->filled_state == 0);
    assert(result->next_hash == 0);
    assert(result->dim_x == 0);
    assert(result->dim_y == 0);
    result->next_hash = *slot;
    result->hash = hash;
    *slot = entry_index;
    table->stats.miss_count++;
  }

  sentinel = vt_internal_glyth_sentinel(table);
  assert(result != sentinel);
  result->next_lru = sentinel->next_lru;
  result->prev_lru = 0;
  next_lru = vt_internal_glyth_entry(table, sentinel->next_lru);
  next_lru->prev_lru = entry_index;
  sentinel->next_lru = entry_index;

  state.id = entry_index;
  state.dim_x = result->dim_x;
  state.dim_y = result->dim_y;
  state.gpu_idx = result->gpu_idx;
  state.filled_state = result->filled_state;
  return state;
}

void
vt_glyth_table_update_entry(VtGlythTable *table, u32 id, u32 new_state, u16 new_dimx, u16 new_dimy)
{
  VtGlythEntry *entry;

  entry = vt_internal_glyth_entry(table, id);
  entry->filled_state = new_state;
  entry->dim_x = new_dimx;
  entry->dim_y = new_dimy;
}

VtGlythCachePoint
vt_glyth_cache_point_unpack(VtGlythIndex idx)
{
  VtGlythCachePoint result;

  result.x = idx.value >> 16;
  result.y = idx.value & 0xffff;
  return result;
}
