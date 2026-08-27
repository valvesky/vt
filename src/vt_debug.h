#pragma once

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

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

static FILE *vt_log_fp;

static void
vt_log_printf(const char *prefix, const char *fmt, ...)
{
  va_list ap;

  if (!vt_log_fp) {
    vt_log_fp = fopen(log_path, "w");
    if (!vt_log_fp)
      vt_log_fp = stderr;
  }

  fputs(prefix, vt_log_fp);
  fputc(' ', vt_log_fp);
  va_start(ap, fmt);
  vfprintf(vt_log_fp, fmt, ap);
  va_end(ap);
  fputc('\n', vt_log_fp);
  fflush(vt_log_fp);
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
