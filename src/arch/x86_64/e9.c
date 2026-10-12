#include "ansi.h"
#include <e9.h>

#include <io.h>
#include <macro.h>
#include <sinks.h>
#include <kprintf.h>

#include <stddef.h>
#include <string.h>

void dputc(int c, void* ctx) {
    UNUSED(ctx);
    _outb(0xE9, c);
}

void e9_print_color(const char* ansi_color) {
    for (size_t i = 0; i < strlen(ansi_color); i++) {
        char c = ansi_color[i];
        dputc(c, NULL);
    }
}

void e9_prefix(int log_lvl) {
    switch (log_lvl) {
        case LOG_WARN:
            e9_print_color(ANSI_COLOR_ORANGE);
            break;
        case LOG_CRIT:
            e9_print_color(ANSI_COLOR_PURPLE);
            break;
        case LOG_TRACE:
            e9_print_color(ANSI_COLOR_GRAY);
            break;
        default:
            e9_print_color(ANSI_COLOR_RESET);
            break;
    }
}

void e9_suffix(int log_lvl) {
    UNUSED(log_lvl);
    e9_print_color(ANSI_COLOR_RESET);
}

static const struct sink e9_sink = {
    .putc = dputc,
    .level_mask = LOG_INFO | LOG_WARN | LOG_CRIT | LOG_TRACE,
    .prefix = e9_prefix,
    .suffix = e9_suffix
};

void e9_init() {
    register_sink(&e9_sink);
}
