#ifndef _CIRC_BUF_C_
#define _CIRC_BUF_C_
#define _GNU_SOURCE

/* Check these out:
 * -> https://en.wikipedia.org/wiki/Circular_buffer
 * -> https://lo.calho.st/posts/black-magic-buffer/ 
 */

#include <time.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

typedef struct CBuffer CBuffer ;

struct CBuffer {
  char *buffer;
  size_t buffer_size;
  int fd;
  size_t read;
  size_t write;
  bool overwrite;
};

#if __GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 27)
static inline int memfd_create(const char *name, unsigned int flags) {
    return syscall(__NR_memfd_create, name, flags);
}
#endif

void cbuffer_init(CBuffer *cb, size_t size);
void cbuffer_destroy(CBuffer *q);
bool cbuffer_push(CBuffer *q, char *data, size_t size);
void cbuffer_push_overwrite(CBuffer *q, char *data, size_t size);
void* cbuffer_read(CBuffer *q, size_t size);

void cbuffer_init(CBuffer *cb, size_t size){

  assert(size % getpagesize() == 0);

  cb->fd = memfd_create("queue_buffer", 0);
  ftruncate(cb->fd, size);

  /* Ask mmap for an address at a location
   * where we can put both virtual copies of the buffer */
  cb->buffer = (char*) mmap(NULL, 2 * size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  /* Map the buffer at that address */
  (void*) mmap(cb->buffer, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, cb->fd, 0);

  /* Now map it again, in the next virtual page */
  (void*) mmap(cb->buffer + size, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, cb->fd, 0);

  cb->read = 0;
  cb->write = 0;
  cb->buffer_size = size;
  cb->overwrite = false;
}

void cbuffer_destroy(CBuffer *q) {
  munmap(q->buffer + q->buffer_size, q->buffer_size);
  munmap(q->buffer, q->buffer_size);
  close(q->fd);
}

/* Pushes data without overwriting: returns false if buffer is full. */
bool cbuffer_push(CBuffer *q, char *data, size_t size) {
  if (size > q->buffer_size) return false;

  if(q->buffer_size - (q->write - q->read) < size) {
    return false;
  }

  memcpy(&q->buffer[q->write], data, size);
  q->write += size;

  return false;
}

void cbuffer_push_overwrite(CBuffer *q, char *data, size_t size) {

  /* edge case: if we are writing an amount of data larger than the buffer */
  if (size > q->buffer_size) {
    data = data+size-1 - q->buffer_size ;
    size = q->buffer_size;
  }

  memcpy(&q->buffer[q->write % q->buffer_size], data, size);
  q->write += size;

  /* "pull along" read pointer when we've overwritten
   * the whole buffer more than once */
  if ( (q->write - q->read) > q->buffer_size ) {
    q->read = q->write-q->buffer_size;
  }
}

void* cbuffer_read(CBuffer *q, size_t size) {
    if(q->write - q->read < size){
        return NULL;
    }
    void *ptr = &q->buffer[q->read % q->buffer_size];
    q->read += size;
    return ptr;
}

#endif
