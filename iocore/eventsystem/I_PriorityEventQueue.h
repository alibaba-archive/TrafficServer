/** @file

  Queue of Events sorted by the "at_timeout" field

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

#ifndef _I_PriorityEventQueue_h_
#define _I_PriorityEventQueue_h_

#include "libts.h"
#include "I_Event.h"

#define TV_INTER(t) ((t) >> 21) // 1 << 21 ns = 2.097152 ms
#define TVR_BITS 8
#define TV_BITS  12
#define TVR_SIZE (1 << TVR_BITS)
#define TV_SIZE (1 << TV_BITS)
#define TVR_MASK (TVR_SIZE - 1)
#define TV_MASK (TV_SIZE - 1)
#define T_MASK 0xffffffffUL

class EThread;

struct PriorityEventQueue
{
  Que(Event, link) tv1[TVR_SIZE];
  Que(Event, link) tv2[TV_SIZE];
  Que(Event, link) tv3[TV_SIZE];
  ink_hrtime last_check_time;
  int32_t now_idx;

  void enqueue(Event * e, ink_hrtime now);
  void remove(Event * e);
  void check_ready(ink_hrtime now, EThread * t);
  int cascade(ink_hrtime now, int heap, int index);
  ink_hrtime earliest_timeout();
  PriorityEventQueue();
};

#endif
