#pragma once

#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

typedef enum Log_Level {
  LOG_LEVEL_FATAL = 0,
  LOG_LEVEL_ERROR,
  LOG_LEVEL_WARN,
  LOG_LEVEL_INFO,
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_TRACE,
  COUNT_LOG_LEVEL
} Log_Level;

static const char *prefix[COUNT_LOG_LEVEL] = {
  [LOG_LEVEL_FATAL] = "[FATAL]",
  [LOG_LEVEL_ERROR] = "[ERROR]",
  [LOG_LEVEL_WARN]  = "[WARNI]",
  [LOG_LEVEL_INFO]  = "[INFOR]",
  [LOG_LEVEL_DEBUG] = "[DEBUG]",
  [LOG_LEVEL_TRACE] = "[TRACE]",
}; 

static_assert(COUNT_LOG_LEVEL == 6, "Log level count changed, make sure to update all the macros.");

static bool log_init(void);
static void log_destroy();

static bool 
log_init()
{
  // TODO: log file
  return true;
}

static void
log_destroy()
{
  // TODO: log file
}

#pragma GCC poison sprintf

static void
log_printf(Log_Level level, const char* src, ...)
{
  char out_message[1024];
  memset(out_message, 0, sizeof(out_message));
  memcpy(out_message, prefix[level], 7); // prefixes are 7 bytes long
  out_message[7] = ' '; // write space

  __builtin_va_list arg_ptr;
  va_start(arg_ptr, src);
  vsnprintf(out_message+8, 1024-8, src, arg_ptr); // append message
  va_end(arg_ptr);

  printf("%.*s\n", 1024, out_message);

  // TODO: write to log file as well
}

#pragma GCC poison printf
#pragma GCC poison puts
#pragma GCC poison perror

/* defining all of these macros might seem silly at first
 * but it is allows to remove the call of log_printf entirely 
 * if we want to enable/disale specific logging levels */

/* '##' operator -> https://gcc.gnu.org/onlinedocs/gcc/Variadic-Macros.html */

#define VTFATAL(message, ...) log_printf(LOG_LEVEL_FATAL, message, ##__VA_ARGS__)
#define VTERROR(message, ...) log_printf(LOG_LEVEL_ERROR, message, ##__VA_ARGS__)

#if LOG_WARN_ENABLED == 1
#define VTWARN(message, ...) log_printf(LOG_LEVEL_WARN, message, ##__VA_ARGS__)
#else
#define VTWARN(message, ...)
#endif

#if LOG_INFO_ENABLED == 1
#define VTINFO(message, ...) log_printf(LOG_LEVEL_INFO, message, ##__VA_ARGS__)
#else 
#define VTINFO(message, ...)
#endif

#if LOG_DEBUG_ENABLED == 1
#define VTDEBUG(message, ...) log_printf(LOG_LEVEL_DEBUG, message, ##__VA_ARGS__)
#else 
#define VTDEBUG(message, ...)
#endif

#if LOG_TRACE_ENABLED == 1
#define VTTRACE(message, ...) log_printf(LOG_LEVEL_TRACE, message, ##__VA_ARGS__)
#else 
#define VTTRACE(message, ...)
#endif

