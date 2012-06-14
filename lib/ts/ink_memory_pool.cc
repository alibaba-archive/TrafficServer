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

/*****************************************************************************


  ****************************************************************************/

#include "ink_config.h"
#include <assert.h>
#include <memory.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "ink_atomic.h"
#include "ink_memory.h"
#include "ink_error.h"
#include "ink_assert.h"
#include "ink_resource.h"
#include "ink_memory_pool.h"


#define RND_ALIGN(_x, align)	(((_x) + (align - 1)) & ~(align - 1))
#define ADDR_ZERO		0x0000
#define POOL_FREE_MEM_LIMIT		(16 * 1024 * 1024)
#define UNFREE_BLOCK_LIMIT		128

static INK_LIST(mem_pools);

void
ink_mem_pool_init(mem_pool *pool,
                  const char *name, uint32_t type_size, uint32_t chunk_size, uint32_t offset, uint32_t alignment,
									uint32_t thread_safe, uint32_t pool_type)
{
  pool->name = name;
  pool->offset = offset;
  /* quick test for power of 2 */
  ink_assert(!(alignment & (alignment - 1)));
  pool->alignment = alignment;
  pool->chunk_size = chunk_size;
  pool->type_size = type_size;
  pool->thread_safe = thread_safe;
  pool->pool_type = pool_type;
  pool->alignment = alignment;
  pool->root = RB_ROOT;

  if (pool->pool_type == POOL_UNALIGN)
	  pool->type_size += sizeof(struct mem_chunk);

  if (pool->alignment)
	  pool->block_size = RND_ALIGN(pool->type_size * pool->chunk_size, pool->alignment);
  else
	  pool->block_size = pool->type_size * pool->chunk_size;

  ink_rwlock_init_ex(&(pool->lock_block_list), pool->thread_safe);
  ink_rwlock_init_ex(&(pool->lock_rbtree), pool->thread_safe);
  ink_mutex_init_ex(&(pool->lock_block), "unfree_block", pool->thread_safe);
  INIT_INK_LIST(&pool->free_block_list);
  INIT_INK_LIST(&pool->unfree_blocks);
	
	pool->unfree_block = 0;
  pool->allocated = 0;
  pool->allocated_base = 0;
  pool->count = 0;
  pool->count_base = 0;

  ink_list_add(&pool->list, &mem_pools);
}


mem_pool *
ink_mem_pool_create(const char *name, uint32_t type_size, uint32_t chunk_size, uint32_t offset, uint32_t alignment,
											uint32_t thread_safe, uint32_t pool_type)
{
  mem_pool *pool = (mem_pool *)ats_malloc(sizeof(mem_pool));
  ink_mem_pool_init(pool, name, type_size, chunk_size, offset, alignment, thread_safe, pool_type);
  return pool;
}

void *
ink_mem_pool_new(mem_pool *pool)
{
	struct ink_list *p = NULL, *n = NULL;
	struct mem_block *block = NULL;
	struct rb64_node *rb64;
	void *chunk = NULL;

	do {
		/* get mem_blcok and tag block->used */
		ink_rwlock_rdlock_ex(&pool->lock_block_list, pool->thread_safe);
		ink_list_for_each_safe(p, n, &pool->free_block_list) {
			block = ink_list_entry(p, struct mem_block, list);
			/* deleted  block struct mem */
			ink_atomic_increment((int *) &block->used, 1);
			if ((chunk = ink_atomiclist_pop(&block->link)))
			  break;
			else
				ink_atomic_increment((int *) &block->used, -1);
		}
		ink_rwlock_unlock_ex(&pool->lock_block_list, pool->thread_safe);

		if (chunk) {
			/* delete block from free_block_list */
			if ((BLOCK_ST_IN == block->status) && (block->used >= pool->chunk_size) && (ink_atomiclist_empty(&block->link))) {
				ink_rwlock_wrlock_ex(&pool->lock_block_list, pool->thread_safe);
				if ((BLOCK_ST_IN == block->status) && (block->used >= pool->chunk_size) && (ink_atomiclist_empty(&block->link))) {
					ink_list_del(&block->list);
					block->status = BLOCK_ST_OUT;
				}
				ink_rwlock_unlock_ex(&pool->lock_block_list, pool->thread_safe);
			}
			ink_atomic_increment((int *) &pool->count, 1);
			return chunk;
		}
		else {
			/* malloc new block then add free_block_list */
			uint32_t i;
			void *newp;

			if (!(newp = mmap(NULL, pool->block_size, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0)))
				continue;

			if (pool->thread_safe) {
				pool->allocated += pool->chunk_size;
			}
			else {
				ink_atomic_increment((int *) &pool->allocated, pool->chunk_size);
			}

			block = (mem_block *)ats_calloc(1, sizeof(mem_block));
			block->mem = newp;
			block->used = 0;
			block->status = BLOCK_ST_IN;
			INIT_INK_LIST(&block->list);

			if (POOL_UNALIGN == pool->pool_type)
				ink_atomiclist_init(&block->link, pool->name, sizeof(struct mem_chunk));
			else
				ink_atomiclist_init(&block->link, pool->name, 0);

			/* free each of the new elements */
			for (i = 0; i < pool->chunk_size; i++) {
				char *a = (char *) newp + i * pool->type_size;
				if (POOL_UNALIGN == pool->pool_type) {
					((struct mem_chunk *)a)->block = block;
					ink_atomiclist_push(&block->link, (void *)(a + sizeof(struct mem_chunk)));
				}
				else	
					ink_atomiclist_push(&block->link, (void *)a);
			}

			if (POOL_ALIGN == pool->pool_type) {
				rb64 = (rb64_node *)ats_malloc(sizeof(rb64_node));
				rb64->key = (char *)newp - (char *)ADDR_ZERO;
				rb64->range = rb64->key + pool->block_size;
				rb64->data = block;

				ink_rwlock_wrlock_ex(&(pool->lock_rbtree), pool->thread_safe);
				rb64_insert(&pool->root, rb64);
				ink_rwlock_unlock_ex(&(pool->lock_rbtree), pool->thread_safe);
			}

			ink_rwlock_wrlock_ex(&pool->lock_block_list, pool->thread_safe);
			ink_list_add(&block->list, &pool->free_block_list);
			ink_rwlock_unlock_ex(&pool->lock_block_list, pool->thread_safe);
		}	
	}
	while (1);
}


void
ink_mem_pool_free(mem_pool *pool, void *item)
{
	struct rb64_node *rb64 = NULL;
	struct mem_block *block = NULL;
	void *chunk = NULL, *memp = NULL;

	if (pool->pool_type == POOL_UNALIGN) {
		chunk = (char *)item - sizeof(struct mem_chunk);
		block = ((struct mem_chunk *)chunk)->block;
	}
	else {
		ink_rwlock_rdlock_ex(&(pool->lock_rbtree), pool->thread_safe);
		rb64 = rb64_lookup(&pool->root, (char *)item - (char *)ADDR_ZERO);
		ink_assert(rb64->key < ((char *)item - (char *)ADDR_ZERO) && rb64->range > ((char *)item - (char *)ADDR_ZERO));
		block = (struct mem_block *)rb64->data;
		ink_rwlock_unlock_ex(&(pool->lock_rbtree), pool->thread_safe);
	}

	ink_atomiclist_push(&block->link, item);
	ink_atomic_increment((int *) &block->used, -1);
	ink_atomic_increment((int *) &pool->count, -1);

	/* recycle some mem unused */
	if ((0 == block->used) && (BLOCK_ST_IN == block->status) && (((pool->allocated - pool->count) * pool->type_size) > POOL_FREE_MEM_LIMIT)) {
		ink_rwlock_wrlock_ex(&pool->lock_block_list, pool->thread_safe);
		if ((BLOCK_ST_IN == block->status) && (0 == block->used)) {
			ink_list_del(&block->list);
			memp = block->mem;
			block->status = BLOCK_ST_OVER;
		}
		ink_rwlock_unlock_ex(&pool->lock_block_list, pool->thread_safe);
		
		if (memp) {
			if (POOL_ALIGN == pool->pool_type) {
				ink_rwlock_wrlock_ex(&(pool->lock_rbtree), pool->thread_safe);
				rb64_delete(&pool->root, rb64);
				ink_rwlock_unlock_ex(&(pool->lock_rbtree), pool->thread_safe);
				ats_free(rb64);
			}
			munmap(memp, pool->block_size);
		
			/* save unfree block */	
			ink_mutex_acquire_ex(&pool->lock_block, pool->thread_safe);
			ink_list_add_tail(&block->list, &pool->unfree_blocks);
			if (pool->unfree_block++ > UNFREE_BLOCK_LIMIT) {
				block = ink_list_first_entry(&pool->unfree_blocks, struct mem_block, list);
				ink_list_del(&block->list);
				ats_free(block);
				pool->unfree_block--;
			}
			ink_mutex_release_ex(&pool->lock_block, pool->thread_safe);

			if (pool->thread_safe) {
				pool->allocated -= pool->chunk_size;
			}
			else {
				ink_atomic_increment((int *) &pool->allocated, -pool->chunk_size);
			}
		}
	}
	else { /* if block is a deleted, then must add tail to free_block_list */
		if (BLOCK_ST_OUT == block->status) {
			ink_rwlock_wrlock_ex(&pool->lock_block_list, pool->thread_safe);
			if (BLOCK_ST_OUT == block->status) {
				ink_list_add_tail(&block->list, &pool->free_block_list);
				block->status = BLOCK_ST_IN;
			}
			ink_rwlock_unlock_ex(&pool->lock_block_list, pool->thread_safe);
		}
	}
}

void
ink_mem_pools_destroy()
{
	struct ink_list *p_p = NULL, *p_n = NULL, *b_p = NULL, *b_n = NULL;
	struct mem_pool *pool = NULL;
	struct mem_block *block = NULL;
	
	ink_list_for_each_safe(p_p, p_n, &mem_pools) {
		pool = ink_list_entry(p_p, struct mem_pool, list);
		ink_list_for_each_safe(b_p, b_n, &pool->free_block_list) {
			block = ink_list_entry(b_p, struct mem_block, list);
			munmap(block->mem, pool->block_size);
			ats_free(block);
		}

		ink_list_for_each_safe(b_p, b_n, &pool->unfree_blocks) {
			block = ink_list_entry(b_p, struct mem_block, list);
			ats_free(block);
		}
	}
}

void
ink_mem_pools_snap_baseline()
{
	struct ink_list *p = NULL, *n = NULL;
	struct mem_pool *pool;

	ink_list_for_each_safe(p, n, &mem_pools) {
			pool = ink_list_entry(p, struct mem_pool, list);
			pool->allocated_base = pool->allocated;
    	pool->count_base = pool->count;
  }
}

void
ink_mem_pools_dump_baselinerel(FILE *f)
{
	struct ink_list *p = NULL, *n = NULL;
	struct mem_pool *pool;
	int a;

  if (f == NULL)
    f = stderr;

	fprintf(f, "     allocated      |       in-use       |  count  | type size  |   free list name\n");
  fprintf(f, "  relative to base  |  relative to base  |         |            |                 \n");
  fprintf(f, "--------------------|--------------------|---------|------------|----------------------------------\n");

	ink_list_for_each_safe(p, n, &mem_pools) {
		pool = ink_list_entry(p, struct mem_pool, list);
		a = pool->allocated - pool->allocated_base;
		if (a != 0) {
			fprintf(f, " %18" PRIu64 " | %18" PRIu64 " | %7u | %10u | memory/%s\n",
							(uint64_t)(pool->allocated - pool->allocated_base) * (uint64_t)pool->type_size,
							(uint64_t)(pool->count - pool->count_base) * (uint64_t)pool->type_size,
							pool->count - pool->count_base, pool->type_size, pool->name ? pool->name : "<unknown>");
		}
	}
}

void
ink_mem_pools_dump(FILE * f)
{
	struct ink_list *p = NULL, *n = NULL;
	struct mem_pool *pool;

	if (f == NULL)
    f = stderr;

  fprintf(f, "     allocated      |        in-use      | type size  |   free list name\n");
  fprintf(f, "--------------------|--------------------|------------|----------------------------------\n");

	ink_list_for_each_safe(p, n, &mem_pools) {
		pool = ink_list_entry(p, struct mem_pool, list);
    fprintf(f, " %18" PRIu64 " | %18" PRIu64 " | %10u | memory/%s\n",
            (uint64_t)pool->allocated * (uint64_t)pool->type_size,
            (uint64_t)pool->count * (uint64_t)pool->type_size, pool->type_size, pool->name ? pool->name : "<unknown>");
  }
}
