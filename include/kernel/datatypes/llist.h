/*
 * Linked List data structure header for allocations.
 */

#ifndef LLIST_H
#define LLIST_H

#include <stddef.h>

struct ll_node {
    size_t len;
    struct ll_node* next;
};

/*
 * Appends a node to the list.
 * @param root pointer to the root node
 * @param node the node to append
 * If root is NULL, node will become root.
 */
void ll_append(struct ll_node** root, struct ll_node* node);

/*
 * Best-fit allocation from a linked list.
 * @param root pointer to the root node
 * @param s the size to allocate
 * @param alloc a function to allocate a new node from memory, can be NULL if none.
 * @returns a pointer to the allocated region, or NULL if s can't be allocated.
 * @note If the root node gets changed internally, the new head node will be written back to *root.
 */
void* llalloc(struct ll_node** root, size_t s, struct ll_node* (*alloc)(size_t));
/*
 * adds a pointer back to the linked list.
 * @param root pointer to the root node
 * @param ptr the pointer to the region
 * @param s the size of the to-be reclaimed region
 * @note If the reclaimed pointer sits at a region before the head node, *root will become the reclaimed pointer.
 */
void llfree(struct ll_node** root, void* ptr, size_t s);

#endif
