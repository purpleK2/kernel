/*
 * Header for Virtual Memory Objects/Contexts and flags
 */

#ifndef VMO_H
#define VMO_H

#include <stddef.h>
#include <stdint.h>

#include <datatypes/bitmap.h>

#define VMM_BASE    0x1000

/* VMO flags */
#define VMO_PRESENT     (1 << 0)    // present
#define VMO_WRITE       (1 << 1)    // write
#define VMO_USR         (1 << 2)    // user
#define VMO_NX          (1 << 3)    // no execute
#define VMO_PALLOC      (1 << 4)    // physical region comes from physically allocated memory (palloc)

#define VMO_KERNEL_READ VMO_PRESENT
#define VMO_KERNEL_RW   VMO_KERNEL_READ | VMO_WRITE
#define VMO_USER_READ   VMO_KERNEL_READ | VMO_USR
#define VMO_USER_RW     VMO_USER_READ | VMO_WRITE

/* Virtual Memory Object */
struct vmo {
    uintptr_t base; // virtual base
    size_t pages;   // PAGESZ blocks
    size_t flags;

    struct vmo* next;   // linked list
};

/* Virtual Memory Context */
struct vmc {
    struct vmo* root_vmo;
    uintptr_t root_table;

    struct bitmap* bitmap;  // keeping track of virtual bases
};

/*
 * Initializes a new VMO.
 * @returns pointer to the newly created VMO.
 */
struct vmo* vmo_new();

/*
 * Initializes a new VMC.
 * @returns pointer to the newly created VMC.
 */
struct vmc* vmc_new();

/*
 * Get a new virtual base address.
 * @param vmc VMC struct object
 * @param pages PAGESZ blocks
 * @param out non-NULL pointer to save the requested address
 * @returns 0 on success, non-zero on failure.
 */
int vmc_new_base(struct vmc* vmc, size_t pages, uintptr_t* out);
/*
 * Reclaim a "virtual pages" to the bitmap.
 * @param vmc VMC struct object
 * @param base address to reclaim
 * @param pages PAGESZ blocks
 */
void vmc_reclaim_base(struct vmc* vmc, uintptr_t base, size_t pages);

/*
 * Append a VMO to a VMC's VMOs list.
 * @param vmc VMC struct object
 * @param vmo VMO to append
 */
void vmc_append(struct vmc* vmc, struct vmo* vmo);

/*
 * Reclaim a VMC struct region.
 * @param v VMC struct object
 */
void vmc_free(struct vmc* v);

/*
 * Reclaim a VMO struct region.
 * @param v VMO struct object
 */
void vmo_free(struct vmo* v);

#endif
