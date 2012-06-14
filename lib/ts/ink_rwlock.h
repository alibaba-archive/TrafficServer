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

//-------------------------------------------------------------------------
// Read-Write Lock -- Code from Stevens' Unix Network Programming -
// Interprocess Communications.  This is the simple implementation and
// will not work if used in conjunction with ink_thread_cancel().
//-------------------------------------------------------------------------

#ifndef _INK_RWLOCK_H_
#define _INK_RWLOCK_H_

#if defined(POSIX_THREAD)
#include <pthread.h>
#include <stdlib.h>
struct ink_rwlock
{
	pthread_rwlock_t rwlock;
};

// just a wrapper so that the constructor gets executed
// before the first call to ink_rwlock_init();
static inline int
ink_rwlock_init(ink_rwlock * rw)
{
#if defined(solaris)
  if ( pthread_rwlock_init(&rw->rwlock, NULL) != 0 ) {
    abort();
  }
#else
  pthread_rwlockattr_t attr;
  
	pthread_rwlockattr_init(&attr);
	pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NP);

  if (pthread_rwlock_init(&rw->rwlock, &attr) != 0) {
    abort();
  }
#endif
  return 0;
}

static inline int
ink_rwlock_init_ex(ink_rwlock * rw, int thread_safe)
{
	if (thread_safe)
		return 0;
	else
		return ink_rwlock_init(rw);
}

static inline int
ink_rwlock_destroy(ink_rwlock * rw)
{
  return pthread_rwlock_destroy(&rw->rwlock);
}

static inline int
ink_rwlock_destroy_ex(ink_rwlock * rw, int thread_safe)
{
	if (thread_safe)
		return 0;
	else
	  return ink_rwlock_destroy(rw);
}

static inline int ink_rwlock_rdlock(ink_rwlock * rw)
{
  if (pthread_rwlock_rdlock(&rw->rwlock) != 0) {
    abort();
  }
  return 0;
}

static inline int
ink_rwlock_rdlock_ex(ink_rwlock * rw, int thread_safe)
{
	if (thread_safe)
		return 0;
	else
		return ink_rwlock_rdlock(rw);
}

static inline int
ink_rwlock_tryrdlock(ink_rwlock * rw)
{
  return pthread_rwlock_tryrdlock(&rw->rwlock) == 0;
}

static inline int
ink_rwlock_tryrdlock_ex(ink_rwlock * rw, int thread_safe)
{
	if (thread_safe)
		return 0;
	else
  	return ink_rwlock_tryrdlock(rw);
}

static inline int ink_rwlock_wrlock(ink_rwlock * rw)
{
  if (pthread_rwlock_wrlock(&rw->rwlock) != 0) {
    abort();
  }
  return 0;
}

static inline int
ink_rwlock_wrlock_ex(ink_rwlock * rw, int thread_safe)
{
	if (thread_safe)
		return 0;
	else
		return ink_rwlock_wrlock(rw);
}

static inline int
ink_rwlock_trywrlock(ink_rwlock * rw)
{
  return pthread_rwlock_trywrlock(&rw->rwlock) == 0;
}

static inline int
ink_rwlock_trywrlock_ex(ink_rwlock * rw, int thread_safe)
{
	if (thread_safe)
		return 0;
	else
  	return ink_rwlock_trywrlock(rw);
}

static inline int
ink_rwlock_unlock(ink_rwlock * rw)
{
  if (pthread_rwlock_unlock(&rw->rwlock) != 0) {
    abort();
  }
  return 0;
}

static inline int
ink_rwlock_unlock_ex(ink_rwlock * rw, int thread_safe)
{
	if (thread_safe)
		return 0;
	else
		return ink_rwlock_unlock(rw);
}

#endif /* #if defined(POSIX_THREAD) */
#endif
