#pragma once

#include <string.h>

static u32 vt_lru_hash_bucket(const VtLRU *l, codepoint_t cp, int for_insert);
static void vt_lru_hash_put(VtLRU *l, codepoint_t cp, vt_glyph_id packed);
static void vt_lru_unlink(VtLRU *l, u32 slot);
static void vt_lru_to_mru(VtLRU *l, u32 slot);

vt_glyph_id
vt_lru_pack(u32 slot)
{
  return ((slot % VT_ATLAS_COLS) << 16) | (slot / VT_ATLAS_COLS);
}

static u32
vt_lru_hash_bucket(const VtLRU *l, codepoint_t cp, int for_insert)
{
  u32 h;
  u32 i;

  if (!l || cp == 0)
    return VT_LRU_NONE;
  h = (cp * 2654435761u) & (VT_GLYPH_MAP_N - 1);
  for (i = 0; i < VT_GLYPH_MAP_N; i++) {
    u32 s = (h + i) & (VT_GLYPH_MAP_N - 1);

    if (l->cp[s] == cp)
      return s;
    if (l->cp[s] == 0)
      return for_insert ? s : VT_LRU_NONE;
  }
  return VT_LRU_NONE;
}

static void
vt_lru_hash_put(VtLRU *l, codepoint_t cp, vt_glyph_id packed)
{
  u32 s;

  s = vt_lru_hash_bucket(l, cp, 1);
  if (s == VT_LRU_NONE)
    return;
  l->cp[s] = cp;
  l->slot[s] = packed;
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
vt_lru_peek(const VtLRU *l, codepoint_t cp)
{
  u32 b;

  b = vt_lru_hash_bucket(l, cp, 0);
  if (b == VT_LRU_NONE)
    return VT_LRU_NONE;
  return l->slot[b];
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
    l->slot_cp[s] = 0;
    l->pin[s] = 0;
    l->color[s] = 0;
    memset(l->cp, 0, sizeof l->cp);
    memset(l->slot, 0, sizeof l->slot);
    {
      u32 i;

      for (i = 0; i < VT_GLYPH_N; i++) {
        if (l->slot_cp[i])
          vt_lru_hash_put(l, l->slot_cp[i],
              vt_lru_pack(i) | (l->color[i] ? VT_GLYPH_COLOR : 0));
      }
    }
    return s;
  }
  return VT_LRU_NONE;
}

void
vt_lru_put(VtLRU *l, codepoint_t cp, u32 slot, int pin, int color)
{
  if (!l || slot >= VT_GLYPH_N || cp == 0)
    return;
  l->slot_cp[slot] = cp;
  l->pin[slot] = pin ? 1 : 0;
  l->color[slot] = color ? 1 : 0;
  vt_lru_hash_put(l, cp, vt_lru_pack(slot) | (color ? VT_GLYPH_COLOR : 0));
  vt_lru_to_mru(l, slot);
}

void
vt_lru_alias(VtLRU *l, codepoint_t cp, u32 slot)
{
  if (!l || slot >= VT_GLYPH_N || cp == 0)
    return;
  vt_lru_hash_put(l, cp, vt_lru_pack(slot));
}
