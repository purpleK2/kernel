#include <mm/vmo.h>
#include <mm/pmm.h>

#include <datatypes/llist.h>

#include <stddef.h>
#include <stdint.h>
#include <macro.h>
#include <memory.h>

static struct ll_node* vmo_list;

/*
 * Allocates a new node for the linked list. This function gets called by llalloc from vmo_new and vmc_new.
 * @param s requested size (can be unaligned)
 * @param out actually allocated size
 * @returns linked list node
 */
struct ll_node* vmm_alloc_llnode(size_t s, size_t* out) {
    *out = ROUND_UP(s, PAGESZ);
    void * p = palloc(ROUND_UP(s, PAGESZ) / PAGESZ);
    return (struct ll_node*)hhdm_virtual((uintptr_t)p);
}

/*
 * Reclaim a vmm linked list node to the PMM.
 * @param p pointer to a reclaimable VMO or VMC struct object
 */
void vmm_free_node(void* p) {
    struct ll_node* n = vmo_list;
    struct ll_node* prev = NULL;
    for (; n != NULL; n = n->next) {
        if ((uintptr_t)n <= (uintptr_t)p && ((uintptr_t)n + (n->len)) > (uintptr_t)p) break;
        prev = n;
    }

    if (!n) return; // something must've gone really wrong but whatever
    if (n->len % PAGESZ != 0) return;

    pfree(n, n->len / PAGESZ);
    if (prev) {
        prev->next = n->next;
    } else {
        vmo_list = n->next;
    }
}

/*
 * Add VMO struct object to a VMC.
 * @param vmc VMC struct object
 * @param vmo VMO struct object to append
 */
void vmc_append(struct vmc* vmc, struct vmo* vmo) {
    if (!vmc) return;
    if (!vmc->root_vmo) {
        vmc->root_vmo = vmo;
        return;
    }

    struct vmo* v = vmc->root_vmo;
    for (; v->next != NULL || vmo->base <= v->base; v = v->next);
    v->next = vmo;
}

struct vmo* vmo_new() {
    struct vmo* v = llalloc(&vmo_list, sizeof(struct vmo), vmm_alloc_llnode);
    memset(v, 0, sizeof(struct vmo));
    return v;
}

int vmc_new_base(struct vmc* vmc, size_t pages, uintptr_t* out) {
    size_t first_block;
    if (bmalloc(vmc->bitmap, pages, &first_block) != 0) {
        return -1;
    }

    size_t idx = BITMAP_IDX(first_block);
    size_t bit = first_block % BITS_PER_BLOCK;

    *out = (PAGESZ * ((BITS_PER_BLOCK * idx) + bit)) + VMM_BASE;
    return 0;
}

void vmc_reclaim_base(struct vmc* vmc, uintptr_t base, size_t pages) {
    base -= VMM_BASE;
    bmfree(vmc->bitmap, base, pages);
}

struct vmc* vmc_new() {
    struct vmc* v = llalloc(&vmo_list, sizeof(struct vmc), vmm_alloc_llnode);
    memset(v, 0, sizeof(struct vmc));
    v->bitmap = llalloc(&vmo_list, sizeof(struct bitmap), vmm_alloc_llnode);
    v->bitmap->bitmap = (bitmap_i*)hhdm_virtual((uintptr_t)palloc(1));  // with one page you keep track of addresses 0x1000-0x8001000 #ungranularity
    v->bitmap->len = PAGESZ / sizeof(bitmap_i);
    v->bitmap->blocks = PAGESZ * BITS_PER_BLOCK;
    return v;
}

void vmc_free(struct vmc* v) {
    llfree(&vmo_list, v, sizeof(struct vmc));
    vmm_free_node(v);
}

void vmo_free(struct vmo* v) {
    llfree(&vmo_list, v, sizeof(struct vmo));
    vmm_free_node(v);
}
