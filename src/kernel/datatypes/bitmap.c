#include <datatypes/bitmap.h>

#include <stddef.h>

int bitmap_find(struct bitmap* bitmap, size_t blocks, size_t *block_out) {
    if (!bitmap || !block_out) return -1;

    size_t cur_blocks = 0;  // counting free blocks
    size_t block = bitmap->last_block;
    for (; block < bitmap->blocks; block++) {
        size_t idx = BITMAP_IDX(block);
        size_t shift = BITMAP_SHITFT(block);
        if ((bitmap->bitmap[idx] & (1 << shift)) == 1) {
            cur_blocks = 0;
            continue;
        }

        if (++cur_blocks == blocks) {
            *block_out = block - (blocks - 1);
            return 0;
        }
    }

    return -2;
}

int bmalloc(struct bitmap* bitmap, size_t blocks, size_t* block) {
    if (!bitmap || !block) return -1;

    if (bitmap_find(bitmap, blocks, block) != 0) {
        // TODO: allocate a new array (alloc function pointer)
        return -1;
    }

    for (size_t b = *block; b < (*block) + blocks; b++) {
        bitmap->bitmap[BITMAP_IDX(b)] |= BITMAP_BIT(b, 1);
    }

    bitmap->last_block = (*block) + blocks;

    return 0;
}

void bmfree(struct bitmap* bitmap, size_t block, size_t blocks) {
    if (!bitmap) return;

    for (size_t b = block; b < block + blocks; b++) {
        bitmap_i i = bitmap->bitmap[BITMAP_IDX(b)];
        i = i & BITMAP_BIT(b, 0);
        bitmap->bitmap[BITMAP_IDX(b)] = i;
    }

    bitmap->last_block = block;
}
