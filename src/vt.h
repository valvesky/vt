#pragma once
#define VT_MAJOR 0
#define VT_MINOR 1
#define VT_PATCH 1

/* CHANGE LOG
 * 0.1.0 - @vasco - Peak Rend Term; ctl; headless
 * 0.1.1 - @vasco - SGR/X10 wheel when child enables mouse
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

#if defined(__clang__) || defined(__gcc__)
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
STATIC_ASSERT(sizeof (u8) == 1, "u8 must be 1 byte");
STATIC_ASSERT(sizeof (i64) == 8, "i64 must be 8 bytes");
STATIC_ASSERT(sizeof (i32) == 4, "i32 must be 4 bytes");
STATIC_ASSERT(sizeof (i16) == 2, "i16 must be 2 bytes");
STATIC_ASSERT(sizeof (i8) == 1, "i8 must be 1 byte");
STATIC_ASSERT(sizeof (f64) == 8, "f64 must be 8 bytes");
STATIC_ASSERT(sizeof (f32) == 4, "f32 must be 4 bytes");

typedef struct Renderer Renderer;
typedef struct Renderer_Cell Renderer_Cell;
