//message.h

#ifndef _MESSAGE_H_
#define _MESSAGE_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "types.h"

struct HelloMessage
{
  uint32_t major;
  uint32_t minor;
  uint32_t min_major;
  uint32_t min_minor;
};

#ifdef __cplusplus
extern "C" {
#endif

#ifndef USE_MULTI_ALLOCATOR
  extern Allocator g_out_message_allocator;
#endif

int cluster_send_msg_internal(const ClusterSession *session,
    SocketContext *pSockContext, const int func_id,
	void *data, const int data_len, const MessagePriority priority);

inline void release_out_message(SocketContext *pSockContext,
    OutMessage *msg)
{
  if (msg->data_type == DATA_TYPE_OBJECT && msg->blocks != NULL) {
    msg->blocks = NULL;
  }
#ifdef USE_MULTI_ALLOCATOR
  pSockContext->out_msg_allocator->free_void(msg);
#else
  g_out_message_allocator.free_void(msg);
#endif
}

#ifdef __cplusplus
}
#endif

#endif

