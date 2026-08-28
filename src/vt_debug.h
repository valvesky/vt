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

#define VT_LOG_N 64
#define VT_LOG_LINE 256

static char vt_log_buf[VT_LOG_N][VT_LOG_LINE];
static u32 vt_log_len[VT_LOG_N];
static u32 vt_log_i;
static u32 vt_log_used;

static void
vt_log_printf(const char *prefix, const char *fmt, ...)
{
  va_list ap;
  char msg[VT_LOG_LINE];
  int pre;
  int n;
  u32 slot;

  pre = snprintf(msg, sizeof msg, "%s ", prefix);
  if (pre < 0)
    pre = 0;
  if ((size_t)pre >= sizeof msg)
    pre = (int)sizeof msg - 1;
  va_start(ap, fmt);
  n = vsnprintf(msg + pre, sizeof msg - (size_t)pre, fmt, ap);
  va_end(ap);
  if (n < 0)
    n = 0;
  n += pre;
  if (n < 0 || (size_t)n >= sizeof msg)
    n = (int)sizeof msg - 1;
  slot = vt_log_i;
  memcpy(vt_log_buf[slot], msg, (size_t)n);
  vt_log_buf[slot][n] = 0;
  vt_log_len[slot] = (u32)n;
  vt_log_i = (vt_log_i + 1u) % VT_LOG_N;
  if (vt_log_used < VT_LOG_N)
    vt_log_used++;
}

#define VTFATAL(message, ...) vt_log_printf("[FATAL]", message, ##__VA_ARGS__)
#define VTERROR(message, ...) vt_log_printf("[ERROR]", message, ##__VA_ARGS__)
#if P_LOG_WARN_ENABLED == 1
#define VTWARN(message, ...)  vt_log_printf("[WARNI]", message, ##__VA_ARGS__)
#else
#define VTWARN(message, ...)
#endif
#if P_LOG_INFO_ENABLED == 1
#define VTINFO(message, ...)  vt_log_printf("[INFOR]", message, ##__VA_ARGS__)
#else
#define VTINFO(message, ...)
#endif
#if P_LOG_DEBUG_ENABLED == 1
#define VTDEBUG(message, ...) vt_log_printf("[DEBUG]", message, ##__VA_ARGS__)
#else
#define VTDEBUG(message, ...)
#endif
#if P_LOG_TRACE_ENABLED == 1
#define VTTRACE(message, ...) vt_log_printf("[TRACE]", message, ##__VA_ARGS__)
#else
#define VTTRACE(message, ...)
#endif
