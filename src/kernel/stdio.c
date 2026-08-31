#include <stdio.h>
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
#include <debug.h>

int debugf(const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);

    int length = npf_vpprintf(dputc, NULL, fmt, va);

    va_end(va);
    return length;
}
