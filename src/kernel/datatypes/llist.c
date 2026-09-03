#include <datatypes/llist.h>

#include <stddef.h>

void ll_append(struct ll_node** root, struct ll_node* node) {
    if (!root || !node) return;
    if (!(*root)) {
        *root = node;
        return;
    }

    struct ll_node* n = *root;
    for (; n->next != NULL; n = n->next);
    n->next = node;
}

void* llalloc(struct ll_node** root, size_t s, struct ll_node* (*alloc)(size_t)) {
    if (!root || !(*root) || !s) return NULL;

    struct ll_node* best_fit = NULL;
    struct ll_node* best_prev = NULL;
    struct ll_node* n_prev = NULL;

    for (struct ll_node* n = *root; n != NULL; n = n->next) {
        if (n->len >= s && (!best_fit || n->len < best_fit->len)) {
            best_fit = n;
            best_prev = n_prev;
        }

        n_prev = n;
    }

    // no best fit found (regions too small?)
    if (!best_fit) {
        if (!alloc) {
            return NULL;
        }

        return alloc(s);    // if possible, allocate a new node from the function
    }

    // Best case: there is a best fit, nothing to do
    // Eventually shift the node
    if (best_fit->len > s) {
        struct ll_node* new = (struct ll_node*)(best_fit + s);
        new->len = best_fit->len - s;
        new->next = best_fit->next;
        if (best_prev) best_prev->next = new;
    }

    return best_fit;
}

void llfree(struct ll_node** root, void* ptr, size_t s) {
    if (!root || !(*root) || !ptr || !s) return;

    struct ll_node* new = ptr;
    new->len = s;

    if (new < *root) {
        new->next = (*root);
        *root = new;
        return;
    }

    struct ll_node* new_prev = *root;
    for (; new_prev != NULL && new_prev < new; new_prev = new_prev->next);
    new->next = new_prev->next;
    new->next = new;
}
