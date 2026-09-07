#pragma once

#include "vt.h"

static __m128i glyph_hash_round(__m128i h, __m128i in);
static int glyph_hash_eq(GlyphHash a, GlyphHash b);
static u32 *glyph_table_slot_pointer(GlyphTable *table, GlyphHash hash);
static GlyphEntry *glyph_table_entry(GlyphTable *table, u32 index);
static void glyph_table_recycle(GlyphTable *table);
static u32 glyph_table_pop_free(GlyphTable *table);

__m128i
glyph_hash_round(__m128i h, __m128i in)
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
glyph_hash_eq(GlyphHash a, GlyphHash b)
{
	__m128i compare;

	compare = _mm_cmpeq_epi32(a.value, b.value);
	return _mm_movemask_epi8(compare) == 0xffff;
}

u32 *
glyph_table_slot_pointer(GlyphTable *table, GlyphHash hash)
{
	u32 hash_index;
	u32 hash_slot;

	hash_index = (u32)_mm_cvtsi128_si32(hash.value);
	hash_slot = hash_index & table->hash_mask;
	VTASSERT(hash_slot < table->hash_count);
	return &table->hash_table[hash_slot];
}

GlyphEntry *
glyph_table_entry(GlyphTable *table, u32 index)
{
	VTASSERT(index < table->entry_count);
	return table->entries + index;
}

void
glyph_table_recycle(GlyphTable *table)
{
	GlyphEntry *sentinel;
	GlyphEntry *entry;
	GlyphEntry *prev;
	u32 *next_index;
	u32 entry_index;

	/* NOTE(vasco): no unused entries left; evict least recently used */
	sentinel = table->entries;
	VTASSERT(sentinel->prev_lru);
	entry_index = sentinel->prev_lru;
	entry = glyph_table_entry(table, entry_index);
	prev = glyph_table_entry(table, entry->prev_lru);
	prev->next_lru = 0;
	sentinel->prev_lru = entry->prev_lru;

	next_index = glyph_table_slot_pointer(table, entry->hash);
	while (*next_index != entry_index) {
		VTASSERT(*next_index);
		next_index = &glyph_table_entry(table, *next_index)->next_hash;
	}
	VTASSERT(*next_index == entry_index);
	*next_index = entry->next_hash;
	entry->next_hash = sentinel->next_hash;
	sentinel->next_hash = entry_index;

	glyph_table_update_entry(table, entry_index, 0, 0, 0);
	table->stats.recycle_count++;
}

u32
glyph_table_pop_free(GlyphTable *table)
{
	GlyphEntry *sentinel;
	GlyphEntry *entry;
	u32 result;

	sentinel = table->entries;
	if (!sentinel->next_hash)
		glyph_table_recycle(table);

	result = sentinel->next_hash;
	VTASSERT(result);
	entry = glyph_table_entry(table, result);
	sentinel->next_hash = entry->next_hash;
	entry->next_hash = 0;

	VTASSERT(entry != sentinel);
	VTASSERT(entry->dim_x == 0);
	VTASSERT(entry->dim_y == 0);
	VTASSERT(entry->filled_state == 0);
	VTASSERT(entry->next_hash == 0);
	return result;
}

size_t
glyph_table_size(GlyphTableParams params)
{
	size_t hash_size;
	size_t entry_size;

	hash_size = params.hash_count * sizeof (u32);
	entry_size = params.entry_count * sizeof (GlyphEntry);
	return sizeof (GlyphTable) + hash_size + entry_size;
}

GlyphTable *
glyph_table_place_in_memory(GlyphTableParams params, void *memory)
{
	GlyphTable *result;
	GlyphEntry *entries;
	u32 starting_tile;
	u32 x;
	u32 y;
	u32 entry_index;

	VTASSERT(memory);
	VTASSERT(params.hash_count >= 1);
	VTASSERT(params.entry_count >= 2);
	VTASSERT(params.hash_count && (params.hash_count & (params.hash_count - 1)) == 0);
	VTASSERT(params.cache_tile_count >= 1);

	/* NOTE(vasco): entries first so __m128i hash loads stay aligned */
	entries = (GlyphEntry *)memory;
	result = (GlyphTable *)(entries + params.entry_count);
	result->hash_table = (u32 *)(result + 1);
	result->entries = entries;
	result->hash_mask = params.hash_count - 1;
	result->hash_count = params.hash_count;
	result->entry_count = params.entry_count;
	memset(result->hash_table, 0, result->hash_count * sizeof *result->hash_table);
	memset(entries, 0, params.entry_count * sizeof (GlyphEntry));

	starting_tile = params.reserved_tile_count;
	x = starting_tile % params.cache_tile_count;
	y = starting_tile / params.cache_tile_count;
	for (entry_index = 0; entry_index < params.entry_count; entry_index++) {
		GlyphEntry *entry;

		if (x >= params.cache_tile_count) {
			x = 0;
			y++;
		}
		entry = glyph_table_entry(result, entry_index);
		if (entry_index + 1 < params.entry_count)
			entry->next_hash = entry_index + 1;
		else
			entry->next_hash = 0;
		entry->gpu_idx.value = (x << 16) | y;
		x++;
	}
	glyph_table_stats(result);
	return result;
}

GlyphTableStats
glyph_table_stats(GlyphTable *table)
{
	GlyphTableStats result;

	result = table->stats;
	memset(&table->stats, 0, sizeof table->stats);
	return result;
}

GlyphState
glyph_table_find_hash(GlyphTable *table, GlyphHash hash)
{
	GlyphEntry *result;
	GlyphEntry *sentinel;
	GlyphEntry *next_lru;
	GlyphEntry *prev;
	GlyphEntry *next;
	u32 *slot;
	u32 entry_index;
	GlyphState state;

	result = 0;
	slot = glyph_table_slot_pointer(table, hash);
	entry_index = *slot;
	while (entry_index) {
		GlyphEntry *entry;

		entry = glyph_table_entry(table, entry_index);
		if (glyph_hash_eq(entry->hash, hash)) {
			result = entry;
			break;
		}
		entry_index = entry->next_hash;
	}

	if (result) {
		VTASSERT(entry_index);
		prev = glyph_table_entry(table, result->prev_lru);
		next = glyph_table_entry(table, result->next_lru);
		prev->next_lru = result->next_lru;
		next->prev_lru = result->prev_lru;
		table->stats.hit_count++;
	} else {
		entry_index = glyph_table_pop_free(table);
		VTASSERT(entry_index);
		result = glyph_table_entry(table, entry_index);
		VTASSERT(result->filled_state == 0);
		VTASSERT(result->next_hash == 0);
		VTASSERT(result->dim_x == 0);
		VTASSERT(result->dim_y == 0);
		result->next_hash = *slot;
		result->hash = hash;
		*slot = entry_index;
		table->stats.miss_count++;
	}

	sentinel = table->entries;
	VTASSERT(result != sentinel);
	result->next_lru = sentinel->next_lru;
	result->prev_lru = 0;
	next_lru = glyph_table_entry(table, sentinel->next_lru);
	next_lru->prev_lru = entry_index;
	sentinel->next_lru = entry_index;

	state.id = entry_index;
	state.dim_x = result->dim_x;
	state.dim_y = result->dim_y;
	state.gpu_idx = result->gpu_idx;
	state.filled_state = result->filled_state;
	return state;
}

GlyphState
glyph_table_peek_hash(GlyphTable *table, GlyphHash hash)
{
	u32 *slot;
	u32 entry_index;
	GlyphState state;

	memset(&state, 0, sizeof state);
	slot = glyph_table_slot_pointer(table, hash);
	entry_index = *slot;
	while (entry_index) {
		GlyphEntry *entry;

		entry = glyph_table_entry(table, entry_index);
		if (glyph_hash_eq(entry->hash, hash)) {
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

void
glyph_table_update_entry(GlyphTable *table, u32 id, u32 new_state, u16 new_dimx, u16 new_dimy)
{
	GlyphEntry *entry;

	entry = glyph_table_entry(table, id);
	entry->filled_state = new_state;
	entry->dim_x = new_dimx;
	entry->dim_y = new_dimy;
}

GlyphCachePoint
glyph_cache_point_unpack(GlyphIndex idx)
{
	GlyphCachePoint result;

	result.x = idx.value >> 16;
	result.y = idx.value & 0xffff;
	return result;
}

GlyphHash
glyph_hash(codepoint_t cp)
{
	/* NOTE(vasco): UTF-8 bytes. 16-byte chunks, 128-bit AES/fallback state. */
	static const u8 seed[16] = {
		178, 201, 95, 240, 40, 41, 143, 216,
		2, 209, 178, 114, 232, 4, 176, 188
	};
	GlyphHash result;
	__m128i h;
	__m128i in;
	u8 tmp[16];

	memset(tmp, 0, sizeof tmp);
	tmp[0] = (u8)cp;
	tmp[1] = (u8)(cp >> 8);
	tmp[2] = (u8)(cp >> 16);
	tmp[3] = (u8)(cp >> 24);
	h = _mm_cvtsi32_si128((int)sizeof (codepoint_t));
	h = _mm_xor_si128(h, _mm_loadu_si128((const __m128i *)seed));
	in = _mm_loadu_si128((const __m128i *)tmp);
	h = glyph_hash_round(h, in);
	result.value = h;
	return result;
}
