/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

/****************************************************************************

****************************************************************************/

#ifndef _INK_LIST_H
#define _INK_LIST_H

#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */

#include <stddef.h>

/*
 *  * These are non-NULL pointers that will result in page faults
 *   * under normal circumstances, used to verify that nobody uses
 *    * non-initialized ink_list entries.
 *     */
#define INK_LIST_POISON1  ((void *) 0x00100100)
#define INK_LIST_POISON2  ((void *) 0x00200200)


/**
 *  * container_of - cast a member of a structure out to the containing structure
 *   * @ptr:	the pointer to the member.
 *    * @type:	the type of the container struct this is embedded in.
 *     * @member:	the name of the member within the struct.
 *      *
 *       */
#define container_of(ptr, type, member) ({			\
        const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
        (type *)( (char *)__mptr - offsetof(type,member) );})

/*
 * Simple doubly linked ink_list implementation.
 *
 * Some of the internal functions ("__xxx") are useful when
 * manipulating whole ink_lists rather than single entries, as
 * sometimes we already know the next/prev entries and we can
 * generate better code by using them directly rather than
 * using the generic single-entry routines.
 */

struct ink_list {
	struct ink_list *next, *prev;
};

#define INK_LIST_INIT(name) { &(name), &(name) }

#define INK_LIST(name) \
	struct ink_list name = INK_LIST_INIT(name)

static inline void INIT_INK_LIST(struct ink_list *list)
{
	list->next = list;
	list->prev = list;
}

/*
 * Insert a entry entry between two known consecutive entries.
 *
 * This is only for internal ink_list manipulation where we know
 * the prev/next entries already!
 */
static inline void __ink_list_add(struct ink_list *entry,
			      struct ink_list *prev,
			      struct ink_list *next)
{
	next->prev = entry;
	entry->next = next;
	entry->prev = prev;
	prev->next = entry;
}

/**
 * ink_list_add - add a entry entry
 * @entry: entry entry to be added
 * @head: ink_list head to add it after
 *
 * Insert a entry entry after the specified head.
 * This is good for implementing stacks.
 */
static inline void ink_list_add(struct ink_list *entry, struct ink_list *head)
{
	__ink_list_add(entry, head, head->next);
}


/**
 * ink_list_add_tail - add a entry entry
 * @entry: entry entry to be added
 * @head: ink_list head to add it before
 *
 * Insert a entry entry before the specified head.
 * This is useful for implementing queues.
 */
static inline void ink_list_add_tail(struct ink_list *entry, struct ink_list *head)
{
	__ink_list_add(entry, head->prev, head);
}

/*
 * Delete a ink_list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal ink_list manipulation where we know
 * the prev/next entries already!
 */
static inline void __ink_list_del(struct ink_list * prev, struct ink_list * next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * ink_list_del - deletes entry from ink_list.
 * @entry: the element to delete from the ink_list.
 * Note: ink_list_empty() on entry does not return true after this, the entry is
 * in an undefined state.
 */
static inline void ink_list_del(struct ink_list *entry)
{
	__ink_list_del(entry->prev, entry->next);
	entry->next = (struct ink_list *)INK_LIST_POISON1;
	entry->prev = (struct ink_list *)INK_LIST_POISON2;
}

/**
 * ink_list_deled - entry is deleted.
 * @entry: the element has deleted.
 * Note: ink_list_empty() on entry does not return true after this, the entry is
 * in an undefined state.
 */
static inline int ink_list_deled(struct ink_list *entry)
{
	return ((entry->next == (struct ink_list *)INK_LIST_POISON1) && (entry->prev == (struct ink_list *)INK_LIST_POISON2));
}


/**
 * ink_list_replace - replace old entry by entry one
 * @old : the element to be replaced
 * @entry : the entry element to insert
 *
 * If @old was empty, it will be overwritten.
 */
static inline void ink_list_replace(struct ink_list *old,
				struct ink_list *entry)
{
	entry->next = old->next;
	entry->next->prev = entry;
	entry->prev = old->prev;
	entry->prev->next = entry;
}

static inline void ink_list_replace_init(struct ink_list *old,
					struct ink_list *entry)
{
	ink_list_replace(old, entry);
	INIT_INK_LIST(old);
}

/**
 * ink_list_del_init - deletes entry from ink_list and reinitialize it.
 * @entry: the element to delete from the ink_list.
 */
static inline void ink_list_del_init(struct ink_list *entry)
{
	__ink_list_del(entry->prev, entry->next);
	INIT_INK_LIST(entry);
}

/**
 * ink_list_move - delete from one ink_list and add as another's head
 * @list: the entry to move
 * @head: the head that will precede our entry
 */
static inline void ink_list_move(struct ink_list *list, struct ink_list *head)
{
	__ink_list_del(list->prev, list->next);
	ink_list_add(list, head);
}

/**
 * ink_list_move_tail - delete from one ink_list and add as another's tail
 * @list: the entry to move
 * @head: the head that will follow our entry
 */
static inline void ink_list_move_tail(struct ink_list *list,
				  struct ink_list *head)
{
	__ink_list_del(list->prev, list->next);
	ink_list_add_tail(list, head);
}

/**
 * ink_list_is_last - tests whether @list is the last entry in ink_list @head
 * @list: the entry to test
 * @head: the head of the ink_list
 */
static inline int ink_list_is_last(const struct ink_list *list,
				const struct ink_list *head)
{
	return list->next == head;
}

/**
 * ink_list_empty - tests whether a ink_list is empty
 * @head: the ink_list to test.
 */
static inline int ink_list_empty(const struct ink_list *head)
{
	return head->next == head;
}

/**
 * ink_list_empty_careful - tests whether a ink_list is empty and not being modified
 * @head: the ink_list to test
 *
 * Description:
 * tests whether a ink_list is empty _and_ checks that no other CPU might be
 * in the process of modifying either member (next or prev)
 *
 * NOTE: using ink_list_empty_careful() without synchronization
 * can only be safe if the only activity that can happen
 * to the ink_list entry is ink_list_del_init(). Eg. it cannot be used
 * if another CPU could re-list_add() it.
 */
static inline int ink_list_empty_careful(const struct ink_list *head)
{
	struct ink_list *next = head->next;
	return (next == head) && (next == head->prev);
}

/**
 * ink_ink_list_is_singular - tests whether a ink_list has just one entry.
 * @head: the ink_list to test.
 */
static inline int ink_list_is_singular(const struct ink_list *head)
{
	return !ink_list_empty(head) && (head->next == head->prev);
}

static inline void __ink_list_cut_position(struct ink_list *list,
		struct ink_list *head, struct ink_list *entry)
{
	struct ink_list *entry_first = entry->next;
	list->next = head->next;
	list->next->prev = list;
	list->prev = entry;
	entry->next = list;
	head->next = entry_first;
	entry_first->prev = head;
}

/**
 * ink_list_cut_position - cut a ink_list into two
 * @list: a entry ink_list to add all removed entries
 * @head: a ink_list with entries
 * @entry: an entry within head, could be the head itself
 *	and if so we won't cut the ink_list
 *
 * This helper moves the initial part of @head, up to and
 * including @entry, from @head to @list. You should
 * pass on @entry an element you know is on @head. @list
 * should be an empty ink_list or a ink_list you do not care about
 * losing its data.
 *
 */
static inline void ink_list_cut_position(struct ink_list *list,
		struct ink_list *head, struct ink_list *entry)
{
	if (ink_list_empty(head))
		return;
	if (ink_list_is_singular(head) &&
		(head->next != entry && head != entry))
		return;
	if (entry == head)
		INIT_INK_LIST(list);
	else
		__ink_list_cut_position(list, head, entry);
}

static inline void __ink_list_splice(const struct ink_list *list,
				 struct ink_list *prev,
				 struct ink_list *next)
{
	struct ink_list *first = list->next;
	struct ink_list *last = list->prev;

	first->prev = prev;
	prev->next = first;

	last->next = next;
	next->prev = last;
}

/**
 * ink_list_splice - join two ink_lists, this is designed for stacks
 * @list: the entry ink_list to add.
 * @head: the place to add it in the first ink_list.
 */
static inline void ink_list_splice(const struct ink_list *list,
				struct ink_list *head)
{
	if (!ink_list_empty(list))
		__ink_list_splice(list, head, head->next);
}

/**
 * ink_list_splice_tail - join two ink_lists, each ink_list being a queue
 * @list: the entry ink_list to add.
 * @head: the place to add it in the first ink_list.
 */
static inline void ink_list_splice_tail(struct ink_list *list,
				struct ink_list *head)
{
	if (!ink_list_empty(list))
		__ink_list_splice(list, head->prev, head);
}

/**
 * ink_list_splice_init - join two ink_lists and reinitialise the emptied ink_list.
 * @list: the entry ink_list to add.
 * @head: the place to add it in the first ink_list.
 *
 * The ink_list at @list is reinitialised
 */
static inline void ink_list_splice_init(struct ink_list *list,
				    struct ink_list *head)
{
	if (!ink_list_empty(list)) {
		__ink_list_splice(list, head, head->next);
		INIT_INK_LIST(list);
	}
}

/**
 * ink_list_splice_tail_init - join two ink_lists and reinitialise the emptied ink_list
 * @list: the entry ink_list to add.
 * @head: the place to add it in the first ink_list.
 *
 * Each of the ink_lists is a queue.
 * The ink_list at @list is reinitialised
 */
static inline void ink_list_splice_tail_init(struct ink_list *list,
					 struct ink_list *head)
{
	if (!ink_list_empty(list)) {
		__ink_list_splice(list, head->prev, head);
		INIT_INK_LIST(list);
	}
}

/**
 * ink_list_entry - get the struct for this entry
 * @ptr:	the &struct ink_list pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the ink_list_struct within the struct.
 */
#define ink_list_entry(ptr, type, member) \
	container_of(ptr, type, member)

/**
 * ink_list_first_entry - get the first element from a ink_list
 * @ptr:	the ink_list head to take the element from.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the ink_list_struct within the struct.
 *
 * Note, that ink_list is expected to be not empty.
 */
#define ink_list_first_entry(ptr, type, member) \
	ink_list_entry((ptr)->next, type, member)

/**
 * ink_list_for_each	-	iterate over a ink_list
 * @pos:	the &struct ink_list to use as a loop cursor.
 * @head:	the head for your ink_list.
 */
#define ink_list_for_each(pos, head) \
	for (pos = (head)->next; prefetch(pos->next), pos != (head); \
        	pos = pos->next)

/**
 * __ink_list_for_each	-	iterate over a ink_list
 * @pos:	the &struct ink_list to use as a loop cursor.
 * @head:	the head for your ink_list.
 *
 * This variant differs from ink_list_for_each() in that it's the
 * simplest possible ink_list iteration code, no prefetching is done.
 * Use this for code that knows the ink_list to be very short (empty
 * or 1 entry) most of the time.
 */
#define __ink_list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

/**
 * ink_list_for_each_prev	-	iterate over a ink_list backwards
 * @pos:	the &struct ink_list to use as a loop cursor.
 * @head:	the head for your ink_list.
 */
#define ink_list_for_each_prev(pos, head) \
	for (pos = (head)->prev; prefetch(pos->prev), pos != (head); \
        	pos = pos->prev)

/**
 * ink_list_for_each_safe - iterate over a ink_list safe against removal of ink_list entry
 * @pos:	the &struct ink_list to use as a loop cursor.
 * @n:		another &struct ink_list to use as temporary storage
 * @head:	the head for your ink_list.
 */
#define ink_list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)

/**
 * ink_list_for_each_prev_safe - iterate over a ink_list backwards safe against removal of ink_list entry
 * @pos:	the &struct ink_list to use as a loop cursor.
 * @n:		another &struct ink_list to use as temporary storage
 * @head:	the head for your ink_list.
 */
#define ink_list_for_each_prev_safe(pos, n, head) \
	for (pos = (head)->prev, n = pos->prev; \
	     prefetch(pos->prev), pos != (head); \
	     pos = n, n = pos->prev)

/**
 * ink_list_for_each_entry	-	iterate over ink_list of given type
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your ink_list.
 * @member:	the name of the ink_list_struct within the struct.
 */
#define ink_list_for_each_entry(pos, head, member)				\
	for (pos = ink_list_entry((head)->next, typeof(*pos), member);	\
	     prefetch(pos->member.next), &pos->member != (head); 	\
	     pos = ink_list_entry(pos->member.next, typeof(*pos), member))

/**
 * ink_list_for_each_entry_reverse - iterate backwards over ink_list of given type.
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your ink_list.
 * @member:	the name of the ink_list_struct within the struct.
 */
#define ink_list_for_each_entry_reverse(pos, head, member)			\
	for (pos = ink_list_entry((head)->prev, typeof(*pos), member);	\
	     prefetch(pos->member.prev), &pos->member != (head); 	\
	     pos = ink_list_entry(pos->member.prev, typeof(*pos), member))

/**
 * ink_list_prepare_entry - prepare a pos entry for use in ink_list_for_each_entry_continue()
 * @pos:	the type * to use as a start point
 * @head:	the head of the ink_list
 * @member:	the name of the ink_list_struct within the struct.
 *
 * Prepares a pos entry for use as a start point in ink_list_for_each_entry_continue().
 */
#define ink_list_prepare_entry(pos, head, member) \
	((pos) ? : ink_list_entry(head, typeof(*pos), member))

/**
 * ink_list_for_each_entry_continue - continue iteration over ink_list of given type
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your ink_list.
 * @member:	the name of the ink_list_struct within the struct.
 *
 * Continue to iterate over ink_list of given type, continuing after
 * the current position.
 */
#define ink_list_for_each_entry_continue(pos, head, member) 		\
	for (pos = ink_list_entry(pos->member.next, typeof(*pos), member);	\
	     prefetch(pos->member.next), &pos->member != (head);	\
	     pos = ink_list_entry(pos->member.next, typeof(*pos), member))

/**
 * ink_list_for_each_entry_continue_reverse - iterate backwards from the given point
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your ink_list.
 * @member:	the name of the ink_list_struct within the struct.
 *
 * Start to iterate over ink_list of given type backwards, continuing after
 * the current position.
 */
#define ink_list_for_each_entry_continue_reverse(pos, head, member)		\
	for (pos = ink_list_entry(pos->member.prev, typeof(*pos), member);	\
	     prefetch(pos->member.prev), &pos->member != (head);	\
	     pos = ink_list_entry(pos->member.prev, typeof(*pos), member))

/**
 * ink_list_for_each_entry_from - iterate over ink_list of given type from the current point
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your ink_list.
 * @member:	the name of the ink_list_struct within the struct.
 *
 * Iterate over ink_list of given type, continuing from current position.
 */
#define ink_list_for_each_entry_from(pos, head, member) 			\
	for (; prefetch(pos->member.next), &pos->member != (head);	\
	     pos = ink_list_entry(pos->member.next, typeof(*pos), member))

/**
 * ink_list_for_each_entry_safe - iterate over ink_list of given type safe against removal of ink_list entry
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your ink_list.
 * @member:	the name of the ink_list_struct within the struct.
 */
#define ink_list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = ink_list_entry((head)->next, typeof(*pos), member),	\
		n = ink_list_entry(pos->member.next, typeof(*pos), member);	\
	     &pos->member != (head); 					\
	     pos = n, n = ink_list_entry(n->member.next, typeof(*n), member))

/**
 * ink_list_for_each_entry_safe_continue
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your ink_list.
 * @member:	the name of the ink_list_struct within the struct.
 *
 * Iterate over ink_list of given type, continuing after current point,
 * safe against removal of ink_list entry.
 */
#define ink_list_for_each_entry_safe_continue(pos, n, head, member) 		\
	for (pos = ink_list_entry(pos->member.next, typeof(*pos), member), 		\
		n = ink_list_entry(pos->member.next, typeof(*pos), member);		\
	     &pos->member != (head);						\
	     pos = n, n = ink_list_entry(n->member.next, typeof(*n), member))

/**
 * ink_list_for_each_entry_safe_from
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your ink_list.
 * @member:	the name of the ink_list_struct within the struct.
 *
 * Iterate over ink_list of given type from current point, safe against
 * removal of ink_list entry.
 */
#define ink_list_for_each_entry_safe_from(pos, n, head, member) 			\
	for (n = ink_list_entry(pos->member.next, typeof(*pos), member);		\
	     &pos->member != (head);						\
	     pos = n, n = ink_list_entry(n->member.next, typeof(*n), member))

/**
 * ink_list_for_each_entry_safe_reverse
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your ink_list.
 * @member:	the name of the ink_list_struct within the struct.
 *
 * Iterate backwards over ink_list of given type, safe against removal
 * of ink_list entry.
 */
#define ink_list_for_each_entry_safe_reverse(pos, n, head, member)		\
	for (pos = ink_list_entry((head)->prev, typeof(*pos), member),	\
		n = ink_list_entry(pos->member.prev, typeof(*pos), member);	\
	     &pos->member != (head); 					\
	     pos = n, n = ink_list_entry(n->member.prev, typeof(*n), member))

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
