#ifndef BITMAP_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t bitmap_i;   // bitmap item
#define BITS_PER_BLOCK      sizeof(bitmap_i) * 8
#define BITMAP_SHITFT(bl)   ((bl) % BITS_PER_BLOCK)
#define BITMAP_IDX(bl)      ((bl) / (BITS_PER_BLOCK))
#define BITMAP_BIT(bl, x)   ((x) << BITMAP_SHITFT(bl))
#define BITMAP_GET(b, i)    ((b)[BITMAP_IDX(i)] & BITMAP_BIT(i, 1))

struct bitmap {
    bitmap_i* bitmap;
    size_t last_block;

    size_t blocks;  // basically the bits
    size_t len;     // array items
};

struct bitmap_offset {
    size_t idx;     // array index
    size_t bits;    // bits offset from idx
};

/*
 * Mark some blocks as used in a bitmap.
 * @param bitmap bitmap struct
 * @param blocks contiguous blocks to mark
 * @param block non-NULL pointer to save the first block of the marked region.
 * @returns 0 on successful allocation, non-zero on error.
 */
int bmalloc(struct bitmap* bitmap, size_t blocks, size_t* block);
/*
 * Reclaim blocks to a bitmap
 * @param bitmap bitmap struct
 * @param the first block of the region
 * @param blocks blocks to reclaim
 */
void bmfree(struct bitmap* bitmap, size_t block, size_t blocks);

#endif
