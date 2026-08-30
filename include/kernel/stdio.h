#ifndef STDIO_H
#define STDIO_H

#define ANSI_COLOR_RED    "\33[31m"
#define ANSI_COLOR_GREEN  "\33[32m"
#define ANSI_COLOR_ORANGE "\33[33m"
#define ANSI_COLOR_GRAY   "\33[90m"
#define ANSI_COLOR_BLUE   "\x1b[38;2;102;163;255m"
#define ANSI_COLOR_RESET  "\33[0m"

/*
 * Formatted print to E9.
 */
int debugf(const char* fmt, ...);

#define debugf_error(fmt, ...)  debugf(ANSI_COLOR_RED fmt ANSI_COLOR_RESET, ##__VA_ARGS__)
#define debugf_ok(fmt, ...)     debugf(ANSI_COLOR_GREEN fmt ANSI_COLOR_RESET, ##__VA_ARGS__)
#define debugf_warn(fmt, ...)   debugf(ANSI_COLOR_ORANGE fmt ANSI_COLOR_RESET, ##__VA_ARGS__)
#define debugf_trace(fmt, ...)   debugf(ANSI_COLOR_GRAY fmt ANSI_COLOR_RESET, ##__VA_ARGS__)

#endif
