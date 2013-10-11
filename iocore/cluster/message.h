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

#ifndef _MESSAGE_H_
#define _MESSAGE_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"

struct HelloMessage
{
  uint32_t major;  //major version
  uint32_t minor;  //minor version
  uint32_t min_major;
  uint32_t min_minor;
};

#ifdef __cplusplus
extern "C" {
#endif

#ifndef USE_MULTI_ALLOCATOR
  extern Allocator out_message_allocator;
#endif

typedef int (*push_to_send_queue_func)(SocketContext *pSockContext, OutMessage *pMessage,
    const MessagePriority priority);

int cluster_send_msg_internal_ex(const ClusterSession *session,
    SocketContext *pSockContext, const int func_id,
	void *data, const int data_len, const MessagePriority priority,
  push_to_send_queue_func push_to_queue_func);

inline void release_out_message(SocketContext *pSockContext,
    OutMessage *msg)
{
  if (msg->data_type == DATA_TYPE_OBJECT && msg->blocks != NULL) {
    msg->blocks = NULL;
  }
#ifdef USE_MULTI_ALLOCATOR
  pSockContext->out_msg_allocator->free_void(msg);
#else
  (void)pSockContext;
  out_message_allocator.free_void(msg);
#endif
}

#ifdef __cplusplus
}
#endif

#endif

