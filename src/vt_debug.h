#pragma once

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(DEBUG)
#define VTASSERT_N(_1, _2, N, ...) N
#define VTASSERT(...) VTASSERT_N(__VA_ARGS__, VTASSERT2, VTASSERT1)(__VA_ARGS__)
#define VTASSERT1(a) assert(a)
#define VTASSERT2(a, s) assert((a) && (s))
#else
#define VTASSERT(...) ((void)0)
#endif

#define VT_TODO \
  do { \
    fprintf(stderr, "VT TODO: %s() in %s:%d\n", __func__, __FILE__, __LINE__); \
    abort(); \
  } while (0)

/* NOTE(vasco):
 * Printing to stderr avoids clogging stdout during benchmarsk
 */
#define vt_logf(prefix, fmt, ...)\
    fprintf(stderr, prefix" "fmt"\n", ##__VA_ARGS__)

/* always print */
#define VTFATAL(message, ...) vt_logf("[FATAL]", message, ##__VA_ARGS__)
#define VTERROR(message, ...) vt_logf("[ERROR]", message, ##__VA_ARGS__)
#define VTWARN(message, ...)  vt_logf("[WARNI]", message, ##__VA_ARGS__)
#define VTINFO(message, ...)  vt_logf("[INFOR]", message, ##__VA_ARGS__)
#define VTTRACE(message, ...) vt_logf("[TRACE]", message, ##__VA_ARGS__)

#if P_LOG_DEBUG_ENABLED == 1
#define VTDEBUG(message, ...) vt_logf("[DEBUG]", message, ##__VA_ARGS__)
#else
#define VTDEBUG(message, ...)
#endif
