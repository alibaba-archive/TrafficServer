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

#ifndef _INK_SPINLOCK_H__
#define _INK_SPINLOCK_H__

#include <pthread.h>
#include <stdlib.h>

typedef pthread_spinlock_t ink_spinlock;

static inline int
ink_spinlock_init(ink_spinlock *spinlock)
{
  if (pthread_spin_init(spinlock, PTHREAD_PROCESS_SHARED) != 0) {
    abort();
  }
  return 0;
}

static inline int
ink_spinlock_destroy(ink_spinlock *spinlock)
{
  return pthread_spin_destroy(spinlock);
}

static inline int
ink_spinlock_acquire(ink_spinlock *spinlock)
{
  if (pthread_spin_lock(spinlock) != 0) {
    abort();
  }
  return 0;
}

static inline int
ink_spinlock_try_acquire(ink_spinlock *spinlock)
{
  return pthread_spin_trylock(spinlock) == 0;
}

static inline int
ink_spinlock_release(ink_spinlock *spinlock)
{
  if (pthread_spin_unlock(spinlock) != 0) {
    abort();
  }
  return 0;
}
#endif //_INK_SPINLOCK_H__
