#pragma once

bool
vt_ring_init(VtRing *ring, size_t pages)
{
  size_t size;

  if (!ring || !pages)
    return false;
  size = pages * peak_page_size();
  ring->base = peak_mirror_map(size);
  ring->size = ring->base ? size : 0;
  ring->r = 0;
  ring->w = 0;
  return ring->base != NULL;
}

void
vt_ring_destroy(VtRing *ring)
{
  if (!ring)
    return;
  if (ring->base)
    peak_mirror_unmap(ring->base, ring->size);
  ring->base = NULL;
  ring->size = 0;
  ring->r = 0;
  ring->w = 0;
}

size_t
vt_ring_unread(const VtRing *ring)
{
  if (!ring || ring->w < ring->r)
    return 0;
  return ring->w - ring->r;
}

size_t
vt_ring_room(const VtRing *ring)
{
  size_t unread;

  if (!ring || !ring->size)
    return 0;
  unread = vt_ring_unread(ring);
  return unread < ring->size ? ring->size - unread : 0;
}

char *
vt_ring_tail(VtRing *ring)
{
  if (!ring || !ring->base || !ring->size)
    return NULL;
  return ring->base + (ring->w % ring->size);
}

bool
vt_ring_produce(VtRing *ring, size_t n)
{
  if (!ring || n > vt_ring_room(ring))
    return false;
  ring->w += n;
  return true;
}

const char *
vt_ring_head(const VtRing *ring)
{
  if (!ring || !ring->base || !ring->size)
    return NULL;
  return ring->base + (ring->r % ring->size);
}

void
vt_ring_consume(VtRing *ring, size_t n)
{
  if (!ring || n > vt_ring_unread(ring))
    return;
  ring->r += n;
}
