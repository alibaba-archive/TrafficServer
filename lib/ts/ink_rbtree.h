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
#ifndef	_INK_RBTREE_H
#define	_INK_RBTREE_H

#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>


struct rb_node
{
	struct rb_node *rb_parent;
	int rb_color;
#define	RB_RED		0
#define	RB_BLACK	1
	struct rb_node *rb_right;
	struct rb_node *rb_left;
};

struct rb_root
{
	struct rb_node *rb_node;
};


#ifndef container_of
#define container_of(ptr, type, member) ({      \
	const typeof( ((type *)0)->member ) *__mptr = (ptr);  \
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif


#define RB_ROOT	(struct rb_root) { NULL, }
#define	rb_entry(ptr, type, member) container_of(ptr, type, member)

extern void rb_insert_color(struct rb_node *, struct rb_root *);
extern void rb_erase(struct rb_node *, struct rb_root *);

/* Find logical next and previous nodes in a tree */
extern struct rb_node *rb_next(struct rb_node *);
extern struct rb_node *rb_prev(struct rb_node *);
extern struct rb_node *rb_first(struct rb_root *);
extern struct rb_node *rb_last(struct rb_root *);

/* Fast replacement of a single node without remove/rebalance/add/rebalance */
extern void rb_replace_node(struct rb_node *victim, struct rb_node *new_node,
			    struct rb_root *root);

static inline void rb_link_node(struct rb_node * node, struct rb_node * parent,
				struct rb_node ** rb_link)
{
	node->rb_parent = parent;
	node->rb_color = RB_RED;
	node->rb_left = node->rb_right = NULL;

	*rb_link = node;
}


/* This structure carries a node, a leaf, and a key. It must start with the
 * rb_node so that it can be cast into an rb_node. We could also have put some
 * sort of transparent union here to reduce the indirection level, but the fact
 * is, the end user is not meant to manipulate internals, so this is pointless.
 */
struct rb64_node {
	struct rb_node node; /* the tree node, must be at the beginning */
	int64_t key;
	int64_t range;
	void	*data;
};

/* Return first node in the tree, or NULL if none */
static inline struct rb64_node *rb64_first(struct rb_root *root)
{
	return rb_entry(rb_first(root), struct rb64_node, node);
}

/* Return rightmost node in the tree, or NULL if none */
static inline struct rb64_node *rb64_last(struct rb_root *root)
{
	return rb_entry(rb_last(root), struct rb64_node, node);
}

/* Return next node in the tree, or NULL if none */
static inline struct rb64_node *rb64_next(struct rb64_node *rb64)
{
	return rb_entry(rb_next(&rb64->node), struct rb64_node, node);
}

/* Return previous node in the tree, or NULL if none */
static inline struct rb64_node *rb64_prev(struct rb64_node *rb64)
{
	return rb_entry(rb_prev(&rb64->node), struct rb64_node, node);
}

/* Delete node from the tree if it was linked in. Mark the node unused. Note
 * that this function relies on a non-inlined generic function: rb_erase
 */
static inline void rb64_delete(struct rb_root *root, struct rb64_node *rb64)
{
	rb_erase(&rb64->node, root);
}

/*
 * Find the first occurrence of the lowest key in the tree <root>, which is 
 * equal to or greater than <x>. NULL is returned is no key matches. 
 */
static inline struct rb64_node *rb64_lookup(struct rb_root *root, int64_t x)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent;
	struct rb64_node *node = NULL;

	while (*p) {
		parent = *p;
		node = rb_entry(parent, struct rb64_node, node);
		if (x < node->key)
			p = &(*p)->rb_left;
		else if (x > node->range)
			p = &(*p)->rb_right;
		else
			break;
	}

	return node;	
}


/* Insert rb64_node <new_node> into subtree starting at node root <root>.
 * Only new_node->key needs be set with the key. The rb64_node is returned.
 * If root->b[EB_RGHT]==1, the tree may only contain unique keys.
 */
static inline struct rb64_node *__rb64_insert(struct rb_root *root, struct rb64_node *new_node)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node * parent = NULL;
	struct rb64_node *node;

	while (*p) {
		parent = *p;
		node = rb_entry(parent, struct rb64_node, node);

		if (new_node->key < node->key)
			p = &(*p)->rb_left;
		else if (new_node->key > node->key)
			p = &(*p)->rb_right;
		else
			return node;
	}

	rb_link_node(&new_node->node, parent, p);

	return NULL;
}

static inline struct rb64_node *rb64_insert(struct rb_root *root, struct rb64_node *new_node)
{
	struct rb64_node *ret = NULL;
	if ((ret == __rb64_insert(root, new_node)))
		goto out;		
	rb_insert_color(&new_node->node, root);
out:
	return ret;
}

#if 0
static inline void print_rbtree(struct rb_root *root)
{
	struct rb64_node *node = rb64_first(root);

	while (node) {
		printf("rbnode=%p key=%lld range=%lld\n", node, node->key, node->range);
		node = rb64_next(node);
	}
}
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
