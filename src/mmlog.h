/*
   @mindmaze_header@
*/
#ifndef MMLOG_H
#define MMLOG_H

#include "mmpredefs.h"
#include <stdlib.h>

#define MMLOG_NONE	-1
#define MMLOG_FATAL	0
#define MMLOG_ERROR	1
#define MMLOG_WARN	2
#define MMLOG_INFO	3
#define MMLOG_DEBUG	4

#ifndef MMLOG_MAXLEVEL
#  define MMLOG_MAXLEVEL	MMLOG_DEBUG
#endif


/*
 Define MMLOG_VERBOSE_LOCATION to display location with file and line
*/
#if MMLOG_VERBOSE_LOCATION
# define MM_LOCATION(module)	 module " " __FILE__ "(" MM_STRINGIFY( __LINE__ ) ")"
#else
# define MM_LOCATION(module)	 module
#endif


#if defined __cplusplus
# define MMLOG_VOID_CAST static_cast<void>
#else
# define MMLOG_VOID_CAST (void)
#endif



#if MMLOG_MAXLEVEL >= MMLOG_FATAL
#  define mmlog_fatal(msg, ...)	mmlog_log(MMLOG_FATAL, MM_LOCATION(MMLOG_MODULE_NAME), msg,  ## __VA_ARGS__)
#else
#  define mmlog_fatal(msg, ...) MMLOG_VOID_CAST(0)
#endif

#if MMLOG_MAXLEVEL >= MMLOG_ERROR
#  define mmlog_error(msg, ...)	mmlog_log(MMLOG_ERROR, MM_LOCATION(MMLOG_MODULE_NAME), msg,  ## __VA_ARGS__)
#else
#  define mmlog_error(msg, ...) MMLOG_VOID_CAST(0)
#endif

#if MMLOG_MAXLEVEL >= MMLOG_WARN
#  define mmlog_warn(msg, ...)	mmlog_log(MMLOG_WARN, MM_LOCATION(MMLOG_MODULE_NAME), msg,  ## __VA_ARGS__)
#else
#  define mmlog_warn(msg, ...) MMLOG_VOID_CAST(0)
#endif

#if MMLOG_MAXLEVEL >= MMLOG_INFO
#  define mmlog_info(msg, ...)	mmlog_log(MMLOG_INFO, MM_LOCATION(MMLOG_MODULE_NAME), msg,  ## __VA_ARGS__)
#else
#  define mmlog_info(msg, ...) MMLOG_VOID_CAST(0)
#endif

#if MMLOG_MAXLEVEL >= MMLOG_DEBUG
#  define mmlog_debug(msg, ...)	mmlog_log(MMLOG_DEBUG, MM_LOCATION(MMLOG_MODULE_NAME), msg,  ## __VA_ARGS__)
#else
#  define mmlog_debug(msg, ...) MMLOG_VOID_CAST(0)
#endif

/* Use mmlog_debug instead, provided only for backward compatibility */
#define mmlog_trace	mmlog_debug

#define mm_crash(msg, ...) do { \
  mmlog_log(MMLOG_FATAL, MM_LOCATION(MMLOG_MODULE_NAME), msg, ## __VA_ARGS__); \
  abort(); } while (0)

#define mm_check(expr, ...) \
  do { if (!(expr)) { \
      mm_crash("MM_CHECK(" #expr ") failed. " __VA_ARGS__); \
    } } while (0)

#ifdef __cplusplus
extern "C" {
#endif

MMLIB_API void mmlog_log(int lvl, const char* location, const char* msg, ...);

MMLIB_API int mmlog_set_maxlvl(int lvl);

#ifdef __cplusplus
}
#endif

#endif /*MMLOG_H*/
