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

#ifndef _ink_memory_pool_h
#define _ink_memory_pool_h

/***********************************************************************

    Generic Queue Implementation (for pointer data types only)

    Uses atomic memory operations to avoid blocking.
    Intended as a replacement for llqueue.


***********************************************************************/

#include "ink_platform.h"
#include "ink_port.h"
#include "ink_apidefs.h"
#include "ink_unused.h"
#include "ink_list.h"
#include "ink_rbtree.h"
#include "ink_mutex.h"
#include "ink_rwlock.h"
#include "ink_queue.h"

/*
  For information on the structure of the x86_64 memory map:

  http://en.wikipedia.org/wiki/X86-64#Linux

  Essentially, in the current 48-bit implementations, the
  top bit as well as the  lower 47 bits are used, leaving
  the upper-but one 16 bits free to be used for the version.
  We will use the top-but-one 15 and sign extend when generating
  the pointer was required by the standard.
*/

#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */

  enum {
    POOL_NULL,
    POOL_ALIGN,
    POOL_UNALIGN
  };

  enum {
    BLOCK_ST_NULL,
    BLOCK_ST_OUT,
    BLOCK_ST_IN,
    BLOCK_ST_OVER
  };

  struct mem_pool
  {
			struct ink_list list;
			struct ink_list free_block_list;
			struct ink_list unfree_blocks;
			struct rb_root root;
			ink_rwlock lock_block_list;
			ink_mutex  lock_block;
			ink_rwlock lock_rbtree;
			const char *name;
			uint32_t type_size, chunk_size, count, allocated, offset, alignment, block_size, thread_safe, pool_type, unfree_block;
			uint32_t allocated_base, count_base;
  };

	struct mem_block {
		struct ink_list list;
		InkAtomicList link;
		volatile uint32_t used, status;
		void *mem;
	};

	struct mem_chunk {
		struct mem_block *block;
	};

/*
 * alignment must be a power of 2
 */
  mem_pool *ink_mem_pool_create(const char *name, uint32_t type_size,
                                   uint32_t chunk_size, uint32_t offset_to_next, uint32_t alignment, 
																	 uint32_t thread_safe, uint32_t pool_type);

  inkcoreapi void ink_mem_pool_init(mem_pool *pool, const char *name,
                                    uint32_t type_size, uint32_t chunk_size,
                                    uint32_t offset_to_next, uint32_t alignment, 
																		uint32_t thread_safe, uint32_t pool_type);
  inkcoreapi void *ink_mem_pool_new(mem_pool *pool);
  inkcoreapi void ink_mem_pool_free(mem_pool *pool, void *item);
  void ink_mem_pools_dump(FILE * f);
  void ink_mem_pools_dump_baselinerel(FILE * f);
  void ink_mem_pools_snap_baseline();
	void ink_mem_pools_destroy();

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ink_queue_h_ */
