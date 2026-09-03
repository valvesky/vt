#pragma once

#include <string.h>

static u32 vt_lru_hash_bucket(const VtLRU *l, VtGlyphHash h, int for_insert);
static void vt_lru_hash_put(VtLRU *l, VtGlyphHash h, vt_glyph_id packed);
static void vt_lru_hash_del(VtLRU *l, VtGlyphHash h);
static void vt_lru_unlink(VtLRU *l, u32 slot);
static void vt_lru_to_mru(VtLRU *l, u32 slot);
static int vt_glyph_hash_empty(VtGlyphHash h);
static int vt_glyph_hash_eq(VtGlyphHash a, VtGlyphHash b);
static __m128i vt_glyph_hash_round(__m128i h, __m128i in);
static VtGlyphHash vt_glyph_hash_cp(codepoint_t cp);

vt_glyph_id
vt_lru_pack(u32 slot)
{
  return ((slot % VT_ATLAS_COLS) << 16) | (slot / VT_ATLAS_COLS);
}

static int
vt_glyph_hash_empty(VtGlyphHash h)
{
  return !(h.w[0] | h.w[1] | h.w[2] | h.w[3]);
}

static int
vt_glyph_hash_eq(VtGlyphHash a, VtGlyphHash b)
{
  return a.w[0] == b.w[0] && a.w[1] == b.w[1] && a.w[2] == b.w[2] && a.w[3] == b.w[3];
}

static __m128i
vt_glyph_hash_round(__m128i h, __m128i in)
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

VtGlyphHash
vt_glyph_hash(const void *bytes, size_t n)
{
  /* NOTE(vasco): refterm ComputeGlyphHash. 16-byte chunks, 128-bit state. */
  static const u8 seed[16] = {
    178, 201, 95, 240, 40, 41, 143, 216,
    2, 209, 178, 114, 232, 4, 176, 188
  };
  const u8 *at;
  VtGlyphHash r;
  __m128i h;
  __m128i in;
  size_t chunks;
  size_t over;
  u8 tmp[16];

  memset(&r, 0, sizeof r);
  at = bytes;
  h = _mm_cvtsi64_si128((long long)n);
  h = _mm_xor_si128(h, _mm_loadu_si128((const __m128i *)seed));
  chunks = n / 16;
  while (chunks--) {
    in = _mm_loadu_si128((const __m128i *)at);
    at += 16;
    h = vt_glyph_hash_round(h, in);
  }
  over = n % 16;
  memset(tmp, 0, sizeof tmp);
  if (over && at)
    memcpy(tmp, at, over);
  in = _mm_loadu_si128((const __m128i *)tmp);
  h = vt_glyph_hash_round(h, in);
  _mm_storeu_si128((__m128i *)r.w, h);
  return r;
}

static VtGlyphHash
vt_glyph_hash_cp(codepoint_t cp)
{
  u32 v;

  v = cp;
  return vt_glyph_hash(&v, sizeof v);
}

static u32
vt_lru_hash_bucket(const VtLRU *l, VtGlyphHash h, int for_insert)
{
  u32 idx;
  u32 i;

  if (!l || vt_glyph_hash_empty(h))
    return VT_LRU_NONE;
  idx = h.w[0] & (VT_GLYPH_MAP_N - 1);
  for (i = 0; i < VT_GLYPH_MAP_N; i++) {
    u32 s = (idx + i) & (VT_GLYPH_MAP_N - 1);

    if (vt_glyph_hash_eq(l->hash[s], h))
      return s;
    if (vt_glyph_hash_empty(l->hash[s]))
      return for_insert ? s : VT_LRU_NONE;
  }
  return VT_LRU_NONE;
}

static void
vt_lru_hash_put(VtLRU *l, VtGlyphHash h, vt_glyph_id packed)
{
  u32 s;

  s = vt_lru_hash_bucket(l, h, 1);
  if (s == VT_LRU_NONE)
    return;
  l->hash[s] = h;
  l->slot[s] = packed;
}

static void
vt_lru_hash_del(VtLRU *l, VtGlyphHash h)
{
  u32 i;
  u32 m;
  VtGlyphHash c;
  vt_glyph_id p;

  i = vt_lru_hash_bucket(l, h, 0);
  if (i == VT_LRU_NONE)
    return;
  m = VT_GLYPH_MAP_N - 1;
  memset(&l->hash[i], 0, sizeof l->hash[i]);
  l->slot[i] = 0;
  for (;;) {
    i = (i + 1u) & m;
    if (vt_glyph_hash_empty(l->hash[i]))
      return;
    c = l->hash[i];
    p = l->slot[i];
    memset(&l->hash[i], 0, sizeof l->hash[i]);
    l->slot[i] = 0;
    vt_lru_hash_put(l, c, p);
  }
}

static void
vt_lru_unlink(VtLRU *l, u32 slot)
{
  u32 p;
  u32 n;

  p = l->prev[slot];
  n = l->next[slot];
  if (p != VT_LRU_NONE)
    l->next[p] = n;
  else if (l->mru == slot)
    l->mru = n;
  if (n != VT_LRU_NONE)
    l->prev[n] = p;
  else if (l->lru == slot)
    l->lru = p;
  l->prev[slot] = VT_LRU_NONE;
  l->next[slot] = VT_LRU_NONE;
}

static void
vt_lru_to_mru(VtLRU *l, u32 slot)
{
  if (l->mru == slot)
    return;
  if (l->mru == VT_LRU_NONE) {
    l->mru = slot;
    l->lru = slot;
    l->prev[slot] = VT_LRU_NONE;
    l->next[slot] = VT_LRU_NONE;
    return;
  }
  if (l->prev[slot] != VT_LRU_NONE || l->next[slot] != VT_LRU_NONE || l->lru == slot)
    vt_lru_unlink(l, slot);
  l->prev[slot] = VT_LRU_NONE;
  l->next[slot] = l->mru;
  l->prev[l->mru] = slot;
  l->mru = slot;
}

void
vt_lru_init(VtLRU *l)
{
  u32 i;

  if (!l)
    return;
  memset(l, 0, sizeof *l);
  for (i = 0; i < VT_GLYPH_N; i++) {
    l->prev[i] = VT_LRU_NONE;
    l->next[i] = VT_LRU_NONE;
  }
  l->mru = VT_LRU_NONE;
  l->lru = VT_LRU_NONE;
}

vt_glyph_id
vt_lru_peek_hash(const VtLRU *l, VtGlyphHash h)
{
  u32 b;

  b = vt_lru_hash_bucket(l, h, 0);
  if (b == VT_LRU_NONE)
    return VT_LRU_NONE;
  return l->slot[b];
}

vt_glyph_id
vt_lru_peek(const VtLRU *l, codepoint_t cp)
{
  if (!cp)
    return VT_LRU_NONE;
  return vt_lru_peek_hash(l, vt_glyph_hash_cp(cp));
}

u32
vt_lru_find(const VtLRU *l, codepoint_t cp)
{
  vt_glyph_id packed;
  u32 col;
  u32 row;
  u32 slot;

  packed = vt_lru_peek(l, cp);
  if (packed == VT_LRU_NONE)
    return VT_LRU_NONE;
  col = (packed >> 16) & 31u;
  row = packed & 0xffu;
  slot = row * VT_ATLAS_COLS + col;
  if (slot >= VT_GLYPH_N)
    return VT_LRU_NONE;
  return slot;
}

void
vt_lru_touch(VtLRU *l, u32 slot)
{
  if (!l || slot >= VT_GLYPH_N)
    return;
  vt_lru_to_mru(l, slot);
}

u32
vt_lru_alloc(VtLRU *l)
{
  u32 s;

  if (!l)
    return VT_LRU_NONE;
  if (l->used < VT_GLYPH_N) {
    s = l->used++;
    l->prev[s] = VT_LRU_NONE;
    l->next[s] = VT_LRU_NONE;
    return s;
  }
  for (s = l->lru; s != VT_LRU_NONE; s = l->prev[s]) {
    if (l->pin[s])
      continue;
    vt_lru_unlink(l, s);
    if (!vt_glyph_hash_empty(l->slot_hash[s]))
      vt_lru_hash_del(l, l->slot_hash[s]);
    memset(&l->slot_hash[s], 0, sizeof l->slot_hash[s]);
    l->pin[s] = 0;
    l->color[s] = 0;
    return s;
  }
  return VT_LRU_NONE;
}

void
vt_lru_put_hash(VtLRU *l, VtGlyphHash h, u32 slot, int pin, int color)
{
  if (!l || slot >= VT_GLYPH_N || vt_glyph_hash_empty(h))
    return;
  l->slot_hash[slot] = h;
  l->pin[slot] = pin ? 1 : 0;
  l->color[slot] = color ? 1 : 0;
  vt_lru_hash_put(l, h, vt_lru_pack(slot) | (color ? VT_GLYPH_COLOR : 0));
  vt_lru_to_mru(l, slot);
}

void
vt_lru_put(VtLRU *l, codepoint_t cp, u32 slot, int pin, int color)
{
  if (!cp)
    return;
  vt_lru_put_hash(l, vt_glyph_hash_cp(cp), slot, pin, color);
}

void
vt_lru_alias(VtLRU *l, codepoint_t cp, u32 slot)
{
  if (!l || slot >= VT_GLYPH_N || cp == 0)
    return;
  vt_lru_hash_put(l, vt_glyph_hash_cp(cp), vt_lru_pack(slot));
}
