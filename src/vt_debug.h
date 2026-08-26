#pragma once

#include <stdarg.h>
#include <stdio.h>

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
