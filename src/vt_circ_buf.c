#pragma once

/* Check these out:
 * -> https://en.wikipedia.org/wiki/Circular_buffer
 * -> https://lo.calho.st/posts/black-magic-buffer/
 */

typedef struct {
  char *buffer;
  size_t buffer_size;
  size_t read;
  size_t write;
} CBuffer;

void cbuffer_init(CBuffer *cb, size_t size);
void cbuffer_destroy(CBuffer *q);
bool cbuffer_push(CBuffer *q, char *data, size_t size);
void cbuffer_push_overwrite(CBuffer *q, char *data, size_t size);
void *cbuffer_read(CBuffer *q, size_t size);

void
cbuffer_init(CBuffer *cb, size_t size)
{
  cb->buffer = peak_mirror_map(size);
  cb->read = 0;
  cb->write = 0;
  cb->buffer_size = cb->buffer ? size : 0;
}

void
cbuffer_destroy(CBuffer *q)
{
  if (q->buffer)
    peak_mirror_unmap(q->buffer, q->buffer_size);
  q->buffer = NULL;
  q->buffer_size = 0;
}

void
cbuffer_push_overwrite(CBuffer *q, char *data, size_t size)
{
  if (size > q->buffer_size) {
    data = data + size - 1 - q->buffer_size;
    size = q->buffer_size;
  }

  memcpy(&q->buffer[q->write % q->buffer_size], data, size);
  q->write += size;

  if ((q->write - q->read) > q->buffer_size)
    q->read = q->write - q->buffer_size;
}

void *
cbuffer_read(CBuffer *q, size_t size)
{
  void *ptr;

  if (q->write - q->read < size)
    return NULL;
  ptr = &q->buffer[q->read % q->buffer_size];
  q->read += size;
  return ptr;
}
