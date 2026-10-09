#include <e9.h>

#include <io.h>
#include <macro.h>
#include <sinks.h>
#include <kprintf.h>

void dputc(int c, void* ctx) {
    UNUSED(ctx);
    _outb(0xE9, c);
}

static const struct sink e9_sink = {
    .putc = dputc,
    .level_mask = LOG_INFO | LOG_WARN | LOG_CRIT | LOG_TRACE
};

void e9_init() {
    register_sink(&e9_sink);
}
