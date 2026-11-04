#include <mm/kvmm.h>

static struct vmc* kvmc;

struct vmc* kvmm_init(uintptr_t root_pt) {
    kvmc = vmc_new();
    kvmc->root_table = root_pt;

    return kvmc;
}

void* kvalloc(size_t pages, size_t flags, uintptr_t phys) {
    return valloc(kvmc, pages, flags, phys);
}

void kvfree(void* p) {
    vfree(kvmc, p);
}
