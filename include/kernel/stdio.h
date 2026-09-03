#ifndef STDIO_H
#define STDIO_H

#define ANSI_COLOR_RED    "\33[31m"         // critical errors
#define ANSI_COLOR_GREEN  "\33[32m"         // success
#define ANSI_COLOR_ORANGE "\33[33m"         // warning
#define ANSI_COLOR_GRAY   "\33[90m"         // trace/log
#define ANSI_COLOR_PURPLE "\33[0;35m"       // panic
#define ANSI_COLOR_RESET  "\33[0m"

#define ANSI_COLOR(c, s)    c s ANSI_COLOR_RESET
#define TRACE_FMT(l)        "[ %s():%d::" l " ] "

/*
 * Formatted print to E9.
 */
int debugf(const char* fmt, ...);

#define debugf_error(fmt, ...)      debugf(ANSI_COLOR(ANSI_COLOR_RED, TRACE_FMT("ERROR") fmt), __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define debugf_ok(fmt, ...)         debugf(ANSI_COLOR(ANSI_COLOR_GREEN, TRACE_FMT("OK") fmt), __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define debugf_warn(fmt, ...)       debugf(ANSI_COLOR(ANSI_COLOR_ORANGE, TRACE_FMT("WARN") fmt), __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define debugf_trace(fmt, ...)      debugf(ANSI_COLOR(ANSI_COLOR_GRAY, TRACE_FMT("trace") fmt), __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define debugf_panic(fmt, ...)      debugf(ANSI_COLOR(ANSI_COLOR_PURPLE, "[ PANIC ] " fmt), ##__VA_ARGS__)

#endif
