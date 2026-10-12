#include <kprintf.h>

#include <stddef.h>
#include <stdarg.h>

#define NANOPRINTF_IMPLEMENTATION
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS          0
#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS    1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS      1
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS          1
#define NANOPRINTF_USE_SMALL_FORMAT_SPECIFIERS          1
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS         1
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS      0
#define NANOPRINTF_USE_ALT_FORM_FLAG                    1
#include <nanoprintf.h>

static struct sink* sink_list;

void register_sink(struct sink* sink) {
    if (!sink_list) {
        sink_list = sink;
        return;
    }

    struct sink* s = sink_list;
    for (; s->next != NULL; s = s->next) {
        if (s == sink) return;
    }
    s->next = sink;
}

int kprintf(enum log_lvl lvl, const char *fmt, ...) {
    va_list va;
    int l = -1;

    for (struct sink* s = sink_list; s != NULL; s = s->next) {
        if (!(s->level_mask & lvl)) continue;

        if (s->prefix) s->prefix(lvl);

        va_start(va, fmt);
        l = npf_vpprintf(s->putc, NULL, fmt, va);
        va_end(va);

        if (s->suffix) s->suffix(lvl);

    }

    return l;
}
