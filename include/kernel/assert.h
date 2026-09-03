#ifndef ASSERT_H
#define ASSERT_H

#include <stdio.h>
#include <stdbool.h>

#define ANSI_ASSERT_OK      ANSI_COLOR_GREEN
#define ANSI_ASSERT_PANIC   ANSI_COLOR_RED

#define assert_debugf(color, fmt, func, line, ...)  debugf(ANSI_COLOR(color, TRACE_FMT("ASSERT") fmt), func, line, ##__VA_ARGS__)

void assert_impl(const char *function, int line, bool condition, char *condtion_str);
#define assert(c)   assert_impl(__FUNCTION__, __LINE__, c, #c)

#endif
