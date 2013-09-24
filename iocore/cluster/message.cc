//message.c

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include "Diags.h"
#include "global.h"
#include "machine.h"
#include "nio.h"
#include "clusterinterface.h"
#include "session.h"
#ifndef TS_INLINE
#define TS_INLINE inline
#endif
#include "I_IOBuffer.h"
#include "P_Cluster.h"
#include "message.h"

#ifndef USE_MULTI_ALLOCATOR
Allocator out_message_allocator("OutMessage", sizeof(OutMessage), 1024);
#endif

inline int64_t get_total_size(IOBufferBlock *blocks) {
  IOBufferBlock *b = blocks;
  int64_t total_avail = 0;
  while (b != NULL) {
    total_avail += b->read_avail();
    b = b->next;
  }
  return total_avail;
}

int cluster_send_message(ClusterSession session, const int func_id,
    void *data, const int data_len, const MessagePriority priority)
{
  MachineSessions *pMachineSessions;
  SessionEntry *pSessionEntry;
  SocketContext *pSockContext;
  OutMessage *pMessage;
  int result;

  if ((result=get_session_for_send(&session, &pMachineSessions,
          &pSessionEntry)) != 0)
  {
    return result;
  }

  pSockContext = pSessionEntry->sock_context;
  if (pSockContext == NULL) {  //session closed
    return ENOENT;
  }

#ifdef USE_MULTI_ALLOCATOR
  pMessage = (OutMessage *)pSockContext->out_msg_allocator->alloc_void();
#else
  pMessage = (OutMessage *)out_message_allocator.alloc_void();
#endif

  if (pMessage == NULL) {
    Error("file: "__FILE__", line: %d, "
        "malloc %d bytes fail, errno: %d, error info: %s",
        __LINE__, (int)sizeof(OutMessage), errno, strerror(errno));
    return errno != 0 ? errno : ENOMEM;
  }

#ifdef MSG_TIME_STAT_FLAG
  int session_index;
  session_index = session.fields.seq % max_session_count_per_machine;
  SESSION_LOCK(pMachineSessions, session_index);

  if (session.fields.ip == my_machine_ip) {  //request by me
    if (pSessionEntry->client_start_time == 0) {
      pSessionEntry->client_start_time = CURRENT_NS();
    }
  }

  if (pSessionEntry->send_start_time == 0) {
    pSessionEntry->send_start_time = CURRENT_NS();
  }

  SESSION_UNLOCK(pMachineSessions, session_index);
#endif

  do {
#ifdef CHECK_MAGIC_NUMBER
    pMessage->header.magic = MAGIC_NUMBER;
#endif
    pMessage->header.func_id = func_id;
    pMessage->header.session_id = session;
    pMessage->header.msg_seq = ink_atomic_increment(
        &pSessionEntry->current_msg_seq, 1) + 1;
    pMessage->in_queue_time = CURRENT_NS();
    pMessage->bytes_sent = 0;
    pMessage->blocks.m_ptr = NULL;
    pMessage->next = NULL;

    if (data_len < 0) {  //object
      pMessage->data_type = DATA_TYPE_OBJECT;
      pMessage->blocks = (IOBufferBlock *)data;
      pMessage->header.data_len = get_total_size(pMessage->blocks);
    }
    else {
      if (data_len > MINI_MESSAGE_SIZE) {
        Error("file: "__FILE__", line: %d, "
            "invalid data length: %d exceeds %d!",
            __LINE__, data_len, MINI_MESSAGE_SIZE);
        result = errno != 0 ? errno : ENOMEM;
        break;
      }

      pMessage->data_type = DATA_TYPE_BUFFER;
      pMessage->blocks = NULL;
      pMessage->header.data_len = data_len;
      memcpy(pMessage->mini_buff, data, data_len);
    }

    pMessage->header.aligned_data_len = BYTE_ALIGN8(
        pMessage->header.data_len);
    result = push_to_send_queue(pSockContext,
        pMessage, priority);
  } while (0);

  if (result != 0) {
    release_out_message(pSockContext, pMessage);
  }

  return result;
}

int cluster_send_msg_internal_ex(const ClusterSession *session,
    SocketContext *pSockContext, const int func_id,
    void *data, const int data_len, const MessagePriority priority,
    push_to_send_queue_func push_to_queue_func)
{
  OutMessage *pMessage;
  int result;

  if (pSockContext == NULL) {  //session closed
    return ENOENT;
  }

#ifdef USE_MULTI_ALLOCATOR
  pMessage = (OutMessage *)pSockContext->out_msg_allocator->alloc_void();
#else
  pMessage = (OutMessage *)out_message_allocator.alloc_void();
#endif

  if (pMessage == NULL) {
    Error("file: "__FILE__", line: %d, "
        "malloc %d bytes fail, errno: %d, error info: %s",
        __LINE__, (int)sizeof(OutMessage), errno, strerror(errno));
    return errno != 0 ? errno : ENOMEM;
  }

  do {
#ifdef CHECK_MAGIC_NUMBER
    pMessage->header.magic = MAGIC_NUMBER;
#endif
    pMessage->header.func_id = func_id;
    pMessage->header.session_id = *session;
    pMessage->header.msg_seq = 11111;
    pMessage->in_queue_time = CURRENT_NS();
    pMessage->bytes_sent = 0;
    pMessage->blocks.m_ptr = NULL;
    pMessage->next = NULL;

    if (data_len < 0) {  //object
      pMessage->data_type = DATA_TYPE_OBJECT;
      pMessage->blocks = (IOBufferBlock *)data;
      pMessage->header.data_len = get_total_size(pMessage->blocks);
    }
    else {
      if (data_len > MINI_MESSAGE_SIZE) {
        Error("file: "__FILE__", line: %d, "
            "invalid data length: %d exceeds %d!",
            __LINE__, data_len, MINI_MESSAGE_SIZE);
        result = errno != 0 ? errno : ENOMEM;
        break;
      }

      pMessage->data_type = DATA_TYPE_BUFFER;
      pMessage->blocks = NULL;
      pMessage->header.data_len = data_len;
      if (data_len > 0) {
        memcpy(pMessage->mini_buff, data, data_len);
      }
    }

    pMessage->header.aligned_data_len = BYTE_ALIGN8(
        pMessage->header.data_len);
    result = push_to_queue_func(pSockContext, pMessage, priority);
  } while (0);

  if (result != 0) {
    release_out_message(pSockContext, pMessage);
  }

  return result;
}

