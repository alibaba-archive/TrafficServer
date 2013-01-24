/** @file

  Queue of Events sorted by the "timeout_at" field impl as binary heap

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

#include "P_EventSystem.h"

PriorityEventQueue::PriorityEventQueue()
{
  last_check_time = ink_get_based_hrtime_internal() / TV_INTER;
}

void 
PriorityEventQueue::enqueue(Event * e, ink_hrtime now)
{
  unsigned long expires = (e->timeout_at / TV_INTER) & T_MASK;
  unsigned long idx = expires - (last_check_time & T_MASK);
  int32_t in_heap, in_idx;

  ink_assert(!e->in_the_prot_queue);
  e->in_the_priority_queue = 1;

  if (((e->timeout_at - now) <= HRTIME_MSECONDS(5)) || ((signed long) idx < 0)) {
    in_heap = 1;
    in_idx = last_check_time & T_MASK & TVR_MASK;
    tv1[in_idx].enqueue(e);
  }   
  else if (idx < 1 << TVR_BITS) {
    in_heap = 1;
    in_idx = expires & TVR_MASK;
    tv1[in_idx].enqueue(e);
  } 
  else if (idx < 1 << (TVR_BITS + TV_BITS)) {
    in_heap = 2;
    in_idx = (expires >> TVR_BITS) & TV_MASK;
    tv2[in_idx].enqueue(e);
  } 
  else {
    if (idx > T_MASK)
      expires = T_MASK + (last_check_time & T_MASK);
    in_heap = 3;
    in_idx = (expires >> (TVR_BITS + TV_BITS)) & TV_MASK;
    tv3[in_idx].enqueue(e);
  }

  e->in_heap = in_heap;
  e->in_idx = in_idx;
  //FIXME: some race will let some cancelled event in pri_queue, but it's acceptable
}

void 
PriorityEventQueue::remove(Event * e)
{
  ink_assert(e->in_the_priority_queue && (e->in_heap >=1) && (e->in_heap <= 3));
  e->in_the_priority_queue = 0;
  switch (e->in_heap) {
    case 1:
      tv1[e->in_idx].remove(e);
      break;
    case 2:
      tv2[e->in_idx].remove(e);
      break;
    case 3: 
      tv3[e->in_idx].remove(e);
  }
}

ink_hrtime 
PriorityEventQueue::earliest_timeout()
{
  int32_t inter = 0;
  int32_t idx = last_check_time & T_MASK & TVR_MASK;
  // THREAD_MAX_HEARTBEAT_MSECONDS = 60
  while (idx < TVR_SIZE && !tv1[idx].head && inter < 60) {
    idx += 2 * inter++;
  }
  return (2 * inter * TV_INTER);
}

int 
PriorityEventQueue::cascade(ink_hrtime now, int heap, int index)
{
  Event *e;
  Que(Event, link)  tv;

  if (heap == 2) { 
    tv = tv2[index];
    tv2[index].clear();
  }
  else if (heap == 3) {
    tv = tv3[index];
    tv3[index].clear();
  }
  while ((e = tv.dequeue())) {
    enqueue(e, now);
  }

  return index;
}

#define INDEX(N) (((last_check_time & T_MASK) >> (TVR_BITS + (N) * TV_BITS)) & TV_MASK)
#define time_after_eq(a,b)  ((long)(a) - (long)(b) >= 0)

void
PriorityEventQueue::check_ready(ink_hrtime now, EThread * t)
{ 
  Event *e;

  t->process_cancel_event(now, t);

  while (time_after_eq(now, last_check_time * TV_INTER)) {
    int index = last_check_time & T_MASK & TVR_MASK;

    if (!index && (!cascade(now, 2, INDEX(0))))
      cascade(now, 3, INDEX(1));

    while ((e = tv1[index].dequeue())) {
      ink_assert(e->in_the_priority_queue && !e->in_the_prot_queue);
      e->in_the_priority_queue = 0;
      if (e->cancelled)
        t->free_event(e);
      else
        t->process_event(e, e->callback_event);
    }
    ++last_check_time;
  }
}
