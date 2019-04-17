/*
 * This is a simple doubly-linked circular list implementation
 * based on the one of the Linux kernel.
 */

#ifndef __LIST_H__
#define __LIST_H__

#include <stdlib.h>
#include <stdbool.h>

#include "util.h"

struct list_head {
    struct list_head* next;
    struct list_head* prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }

/**
 * Initializes a new list item.
 */
static inline void list_init(struct list_head* item) {
    item->next = item;
    item->prev = item;
}

/**
 * Checks if the given list head represents an empty list.
 */
static inline bool list_empty(const struct list_head* head) {
    return head->next == head;
}

/**
 * Adds a new item at the beginning of the list
 */
static inline void list_add(struct list_head* head, struct list_head* item) {
    struct list_head* prev = head;
    struct list_head* next = head->next;

    next->prev = item;
	item->next = next;
	item->prev = prev;
	prev->next = item;
}

/**
 * Adds a new item at the end of the list
 */
static inline void list_add_tail(struct list_head* head, struct list_head* item) {
    struct list_head* prev = head->prev;
    struct list_head* next = head;

    next->prev = item;
	item->next = next;
	item->prev = prev;
	prev->next = item;
}

/**
 * Removes an item from the list it is in by making the previous
 * and next element point to each other.
 */
static inline void list_del(struct list_head* item) {
    
    item->next->prev = item->prev;
    item->prev->next = item->next;

    // Invalidate the pointers of the removed item
    item->next = NULL;
    item->prev = NULL;

}

/** Returns the next item in a linked list. **/
#define list_next(el, type, member) \
    container_of((el)->member.next, type, member)

/** Returns the prev item in a linked list. **/
#define list_prev(el, type, member) \
    container_of((el)->member.prev, type, member)

/** Returns the first item of a list, or NULL if the list is empty. */
#define list_first(head, type, member) \
    (list_empty(head) ? NULL : container_of((head)->next, type, member))

/** Returns the last item of a list, or NULL if the list is empty. */
#define list_last(head, type, member) \
    (list_empty(head) ? NULL : container_of((head)->prev, type, member))

/** Iterate over a list. */
#define list_for_each(pos, head) \
	for (struct list_head *pos = (head)->next, *__n = pos->next; \
        pos != (head); \
		pos = __n, __n = pos->next)

/** Iterate backwards over a list. */
#define list_for_each_rev(pos, head) \
	for (struct list_head *pos = (head)->prev, *__n = pos->prev; \
	     pos != (head); \
	     pos = __n, __n = pos->prev)

/** Iterate a list of structures that contain a struct list_head. */
#define list_for_each_member(pos, head, type, member) \
    for (type *pos = container_of((head)->next, type, member), *__n = container_of(pos->member.next, type, member); \
        &pos->member != (head); \
        pos = __n, __n = container_of(pos->member.next, type, member))

/** Iterate a list of structures that contain a struct list_head. */
#define list_for_each_rev_member(pos, head, type, member) \
    for (type *pos = container_of((head)->prev, type, member), *__n = container_of(pos->member.prev, type, member); \
        &pos->member != (head); \
        pos = __n, __n = container_of(pos->member.prev, type, member))

/** Iterate a portion of a list from one node to another. */
#define list_for_each_interval(pos, from, to, type, member) \
    for (type *pos = (from), *__n = container_of(pos->member.next, type, member), *__stop = (type*)(pos == (to) ? 1L : 0L); \
        (long)__stop < 2L; \
        pos = __n, __n = container_of(pos->member.next, type, member), __stop = (type*)((long)__stop == 1L ? 2L : (pos == (to) ? 1L : 0L)))

/** Iterate a portion of a list from one node to another in the reverse order. */
#define list_for_each_rev_interval(pos, from, to, type, member) \
    for (type *pos = (to), *__n = container_of(pos->member.prev, type, member), *__stop = (type*)(pos == (from) ? 1L : 0L); \
        (long)__stop < 2L; \
        pos = __n, __n = container_of(pos->member.prev, type, member), __stop = (type*)((long)__stop == 1L ? 2L : (pos == (from) ? 1L : 0L)))


#endif