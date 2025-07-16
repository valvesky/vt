#ifndef _VT_ARRAY_C_
#define _VT_ARRAY_C_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define RESIZE_FACTOR 1.618

typedef unsigned char byte;

typedef struct DArray {
  void *array;
  size_t len;
  size_t capacity;
  size_t type;
} DArray;

typedef struct {
  DArray* arr;
  size_t len;
  size_t capacity;
  size_t type;
} Array2D ;

DArray darray_init(size_t type, size_t capacity) {
  DArray new = {0};
  new.type = type;
  new.array = calloc(capacity, type);
  new.capacity = capacity;
  return new;
}

void darray_resize(DArray *darray) {

  size_t new_cap = darray->capacity * RESIZE_FACTOR;
  void* new = realloc(darray->array, new_cap*darray->type);

  if (new) {
    darray->array = new;
    memset(darray->array+(darray->capacity*darray->type), 0, (new_cap-darray->capacity) * darray->type);
    darray->capacity = new_cap;
  }
}

void darray_destroy(DArray *darray) {
  free(darray->array);
  memset(darray, 0, sizeof *darray);
}

void* darray_get(DArray *darray, size_t idx) {
  return  darray->array + (idx*darray->type);
}

void darray_insert(DArray *darray, void *src ) {
  if (darray->len == darray->capacity)
    darray_resize(darray);

  void *dest = darray_get(darray, darray->len++);
  memcpy(dest, src, darray->type);
}

void darray_print(DArray *darray) {

  void *ptr = darray->array;
  void *end = ptr + (darray->len * darray->type);
  while (ptr < end) {
    if ( (long) ptr % (30*darray->type) == 0) puts("");
    printf("%3d ", * ((int*)ptr) ); ptr += darray->type;
  }
  puts("");
}

void darray_print_hex(DArray *darray) {

  void *ptr = darray->array;
  void *end = ptr + (darray->len * darray->type);
  while (ptr < end) {
    for (size_t i = 0; i < darray->type * 2; i++) {
        printf("%02X ", *((byte*)ptr)>>(i*2));
    }
    puts("");
    ptr += darray->type;
  }
  puts("");
}


#endif
