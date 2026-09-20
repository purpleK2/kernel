#ifndef MACRO_H
#define MACRO_H

#define LIMINEREQ   __attribute__((used, section(".limine_requests")))
#define PACKED      __attribute__((packed))
#define ALIGNED(x)  __attribute__((aligned(x)))

#define UNUSED(x) ((void)x)

#define ROUND_DOWN(n, a)    ((n) & ~((a) - 1))
#define ROUND_UP(n, a)      (((n) + ((a) - 1)) & ~((a) - 1))

#endif
