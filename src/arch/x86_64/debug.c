#include <debug.h>

#include <io.h>
#include <macro.h>

void dputc(int c, void* ctx) {
    UNUSED(ctx);
    _outb(0xE9, c);
}
