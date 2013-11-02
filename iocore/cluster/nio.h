//nio.h

#ifndef _NIO_H_
#define _NIO_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "types.h"
#include "clusterinterface.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct worker_thread_context *g_worker_thread_contexts;
extern int g_worker_thread_count;

extern message_deal_func g_msg_deal_func;
extern machine_change_notify_func g_machine_change_notify;

int nio_init();
int nio_destroy();

int nio_add_to_epoll(SocketContext *pSockContext);
int push_to_send_queue(SocketContext *pSockContext, OutMessage *pMessage,
    const MessagePriority priority, const uint32_t sessionVersion);

int insert_into_send_queue_head(SocketContext *pSockContext, OutMessage *pMessage,
    const MessagePriority priority);

void log_nio_stats();

#ifdef __cplusplus
}
#endif

#endif

