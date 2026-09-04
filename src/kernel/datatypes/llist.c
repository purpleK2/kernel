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
        // best-fit
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

    struct ll_node* new_next;   // new best_prev->next node
    if (best_fit->len > s) {
        // this node still has some space, create a new one after allocated space
        new_next = (struct ll_node*)(best_fit + s);
        new_next->len = best_fit->len - s;
        new_next->next = best_fit->next;
    } else {
        // there is no more space in this region
        new_next = best_fit->next;
    }

    if (best_prev) best_prev->next = new_next;               // link previous node to this one
    if (*root != new_next && best_fit == *root) *root = new_next; // if the root node changed, write it back

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
