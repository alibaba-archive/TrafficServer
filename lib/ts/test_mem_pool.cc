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

#include <stdlib.h>
#include <string.h>
#include "ink_thread.h"
#include "ink_queue.h"
#include "ink_memory_pool.h"
#include "ink_unused.h" /* MAGIC_EDITING_TAG */
#include "libts.h"


#define NTHREADS 32
static int num_test_calls = 0;

void *
test(void *p)
{
  int id = 1;
  void *m1, *m2, *m3;
	struct mem_pool *pool = (struct mem_pool *)p;

  time_t start = time(NULL);
  int count = 0;
  for (;;) {
    m1 = ink_mem_pool_new(pool);
    m2 = ink_mem_pool_new(pool);
    m3 = ink_mem_pool_new(pool);

    if ((m1 == m2) || (m1 == m3) || (m2 == m3)) {
      printf("0x%08" PRIx64 "   0x%08" PRIx64 "   0x%08" PRIx64 "\n",
             (uint64_t)(uintptr_t)m1, (uint64_t)(uintptr_t)m2, (uint64_t)(uintptr_t)m3);
      exit(1);
    }

    //memset(m1, id, 64);
    //memset(m2, id, 64);
    //memset(m3, id, 64);

    ink_mem_pool_free(pool, m1);
    ink_mem_pool_free(pool, m2);
    ink_mem_pool_free(pool, m3);

		ink_atomic_increment(&num_test_calls, 1);
    // break out of the test if we have run more then 60 seconds
    if (++count % 1000 == 0 && (start + 10) < time(NULL)) {
			//printf("allocated: %lld  count: %lld  unfree_block: %lld\n", pool->allocated, pool->count, pool->unfree_block);
      //return NULL;
    }
  }
}


int
main(int argc, char *argv[])
{
  int i;

	struct mem_pool *align_pool, *unalign_pool;
  align_pool = ink_mem_pool_create("align", 64, 4, 0, 8, 0, POOL_ALIGN);
  unalign_pool = ink_mem_pool_create("unalign", 64, 4, 0, 8, 0, POOL_UNALIGN);

  for (i = 0; i < NTHREADS; i++) {
    fprintf(stderr, "Create thread %d\n", i);
    ink_thread_create(test, (void *)unalign_pool);
    ink_thread_create(test, (void *)align_pool);
  }

  test((void *) unalign_pool);
  test((void *) align_pool);

	fprintf(stderr, "total test calls is %d\n", num_test_calls);
  return 0;
}
