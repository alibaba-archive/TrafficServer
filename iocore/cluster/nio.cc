//nio.c

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
#include <sys/prctl.h>
#include "Diags.h"
#include "global.h"
#include "shared_func.h"
#include "pthread_func.h"
#include "sockopt.h"
#include "session.h"
#include "message.h"
#include "connection.h"
#ifndef TS_INLINE
#define TS_INLINE inline
#endif
#include "I_IOBuffer.h"
#include "P_Cluster.h"
#include "P_RecCore.h"
#include "ink_config.h"
#include "nio.h"

int g_worker_thread_count = 0;

struct worker_thread_context *g_worker_thread_contexts = NULL;

static pthread_mutex_t worker_thread_lock;
//static char magic_buff[4];

static void *work_thread_entrance(void* arg);

message_deal_func g_msg_deal_func = NULL;
machine_change_notify_func g_machine_change_notify = NULL;

struct NIORecords {
  RecRecord * call_writev_count;
  RecRecord * send_retry_count;
  RecRecord * call_read_count;

  RecRecord * write_wait_time;
  RecRecord * epoll_wait_count;
  RecRecord * epoll_wait_time_used;
  RecRecord * loop_usleep_count;
  RecRecord * loop_usleep_time;
  RecRecord * io_loop_interval;

  RecRecord * max_write_loop_time_used;
  RecRecord * max_read_loop_time_used;
  RecRecord * max_epoll_time_used;
  RecRecord * max_usleep_time_used;
  RecRecord * max_callback_time_used;
};

static NIORecords nio_records = {NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static int write_wait_time = 1 * HRTIME_MSECOND;   //write wait time calc by cluster IO
static int io_loop_interval = 0;  //us

static volatile int64_t max_write_loop_time_used = 0;
static volatile int64_t max_read_loop_time_used = 0;
static volatile int64_t max_epoll_time_used = 0;
static volatile int64_t max_usleep_time_used = 0;
static volatile int64_t max_callback_time_used = 0;

inline int get_iovec(IOBufferBlock *blocks, IOVec *iovec, int size) {
  int niov;
  IOBufferBlock *b = blocks;
  niov = 0;
  while (b != NULL && niov < size) {
    int64_t a = b->read_avail();
    if (a > 0) {
      iovec[niov].iov_len = a;
      iovec[niov].iov_base = b->_start;
      ++niov;
    }
    b = b->next;
  }

  return niov;
}

inline void consume(OutMessage *pMessage, int64_t l) {
  while (pMessage->blocks != NULL) {
    int64_t r = pMessage->blocks->read_avail();
    if (l < r) {
      pMessage->blocks->consume(l);
      break;
    } else {
      l -= r;
      pMessage->blocks = pMessage->blocks->next;
    }
  }
}

static void init_nio_stats()
{
  RecData data_default;
  memset(&data_default, 0, sizeof(RecData));

  RecRegisterStatInt(RECT_PROCESS, "proxy.process.cluster.io.send_msg_count", 0, RECP_NON_PERSISTENT);
  RecRegisterStatInt(RECT_PROCESS, "proxy.process.cluster.io.send_bytes", 0, RECP_NON_PERSISTENT);
  RecRegisterStatInt(RECT_PROCESS, "proxy.process.cluster.io.recv_msg_count", 0, RECP_NON_PERSISTENT);
  RecRegisterStatInt(RECT_PROCESS, "proxy.process.cluster.io.recv_bytes", 0, RECP_NON_PERSISTENT);

  nio_records.call_writev_count = RecRegisterStat(RECT_PROCESS,
      "proxy.process.cluster.io.call_writev_count", RECD_INT, data_default, RECP_NON_PERSISTENT);
  nio_records.send_retry_count = RecRegisterStat(RECT_PROCESS,
      "proxy.process.cluster.io.send_retry_count", RECD_INT, data_default, RECP_NON_PERSISTENT);
  nio_records.call_read_count = RecRegisterStat(RECT_PROCESS,
      "proxy.process.cluster.io.call_read_count", RECD_INT, data_default, RECP_NON_PERSISTENT);
  nio_records.epoll_wait_count = RecRegisterStat(RECT_PROCESS,
      "proxy.process.cluster.io.epoll_wait_count", RECD_INT, data_default, RECP_NON_PERSISTENT);
  nio_records.epoll_wait_time_used = RecRegisterStat(RECT_PROCESS,
      "proxy.process.cluster.io.epoll_wait_time_used", RECD_INT, data_default, RECP_NON_PERSISTENT);
  nio_records.loop_usleep_count = RecRegisterStat(RECT_PROCESS,
      "proxy.process.cluster.io.loop_usleep_count", RECD_INT, data_default, RECP_NON_PERSISTENT);
  nio_records.loop_usleep_time = RecRegisterStat(RECT_PROCESS,
      "proxy.process.cluster.io.loop_usleep_time", RECD_INT, data_default, RECP_NON_PERSISTENT);
  nio_records.write_wait_time = RecRegisterStat(RECT_PROCESS,
      "proxy.process.cluster.io.write_wait_time", RECD_INT, data_default, RECP_NON_PERSISTENT);
  nio_records.io_loop_interval = RecRegisterStat(RECT_PROCESS,
      "proxy.process.cluster.io.loop_interval", RECD_INT, data_default, RECP_NON_PERSISTENT);

  RecRegisterStatInt(RECT_PROCESS, "proxy.process.cluster.ping_total_count", 0, RECP_NON_PERSISTENT);
  RecRegisterStatInt(RECT_PROCESS, "proxy.process.cluster.ping_success_count", 0, RECP_NON_PERSISTENT);
  RecRegisterStatInt(RECT_PROCESS, "proxy.process.cluster.ping_time_used", 0, RECP_NON_PERSISTENT);
  RecRegisterStatInt(RECT_PROCESS, "proxy.process.cluster.io.send_delayed_time", 0, RECP_NON_PERSISTENT);
  RecRegisterStatInt(RECT_PROCESS, "proxy.process.cluster.io.push_msg_count", 0, RECP_NON_PERSISTENT);
  RecRegisterStatInt(RECT_PROCESS, "proxy.process.cluster.io.push_msg_bytes", 0, RECP_NON_PERSISTENT);

  nio_records.max_write_loop_time_used = RecRegisterStat(RECT_PROCESS,
      "proxy.process.cluster.io.max_write_loop_time_used", RECD_INT, data_default, RECP_NON_PERSISTENT);
  nio_records.max_read_loop_time_used = RecRegisterStat(RECT_PROCESS,
      "proxy.process.cluster.io.max_read_loop_time_used", RECD_INT, data_default, RECP_NON_PERSISTENT);
  nio_records.max_epoll_time_used = RecRegisterStat(RECT_PROCESS,
      "proxy.process.cluster.io.max_epoll_time_used", RECD_INT, data_default, RECP_NON_PERSISTENT);
  nio_records.max_usleep_time_used = RecRegisterStat(RECT_PROCESS,
      "proxy.process.cluster.io.max_usleep_time_used", RECD_INT, data_default, RECP_NON_PERSISTENT);
  nio_records.max_callback_time_used = RecRegisterStat(RECT_PROCESS,
      "proxy.process.cluster.io.max_callback_time_used", RECD_INT, data_default, RECP_NON_PERSISTENT);
}

void log_nio_stats()
{
  RecData data;
	struct worker_thread_context *pThreadContext;
	struct worker_thread_context *pContextEnd;
  SocketStats sum = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  static time_t last_calc_Bps_time = CURRENT_TIME();
  static int64_t last_send_bytes = 0;

	pContextEnd = g_worker_thread_contexts + g_work_threads;
	for (pThreadContext=g_worker_thread_contexts; pThreadContext<pContextEnd;
      pThreadContext++)
	{
    sum.send_msg_count += pThreadContext->stats.send_msg_count;
    sum.send_bytes += pThreadContext->stats.send_bytes;
    sum.call_writev_count += pThreadContext->stats.call_writev_count;
    sum.send_retry_count += pThreadContext->stats.send_retry_count;
    sum.recv_msg_count += pThreadContext->stats.recv_msg_count;
    sum.recv_bytes += pThreadContext->stats.recv_bytes;
    sum.call_read_count += pThreadContext->stats.call_read_count;
    sum.epoll_wait_count += pThreadContext->stats.epoll_wait_count;
    sum.epoll_wait_time_used += pThreadContext->stats.epoll_wait_time_used;
    sum.loop_usleep_count += pThreadContext->stats.loop_usleep_count;
    sum.loop_usleep_time += pThreadContext->stats.loop_usleep_time;
    sum.ping_total_count += pThreadContext->stats.ping_total_count;
    sum.ping_success_count += pThreadContext->stats.ping_success_count;
    sum.ping_time_used += pThreadContext->stats.ping_time_used;
    sum.send_delayed_time += pThreadContext->stats.send_delayed_time;
    sum.push_msg_count += pThreadContext->stats.push_msg_count;
    sum.push_msg_bytes += pThreadContext->stats.push_msg_bytes;
  }

  data.rec_int = sum.send_msg_count;
  RecSetRecord(RECT_PROCESS, "proxy.process.cluster.io.send_msg_count", RECD_INT, &data, NULL); 
  data.rec_int = sum.send_bytes;
  RecSetRecord(RECT_PROCESS, "proxy.process.cluster.io.send_bytes", RECD_INT, &data, NULL); 
  data.rec_int = sum.recv_msg_count;
  RecSetRecord(RECT_PROCESS, "proxy.process.cluster.io.recv_msg_count", RECD_INT, &data, NULL); 
  data.rec_int = sum.recv_bytes;
  RecSetRecord(RECT_PROCESS, "proxy.process.cluster.io.recv_bytes", RECD_INT, &data, NULL); 
  data.rec_int = sum.ping_total_count;
  RecSetRecord(RECT_PROCESS, "proxy.process.cluster.ping_total_count", RECD_INT, &data, NULL); 
  data.rec_int = sum.ping_success_count;
  RecSetRecord(RECT_PROCESS, "proxy.process.cluster.ping_success_count", RECD_INT, &data, NULL); 
  data.rec_int = sum.ping_time_used;
  RecSetRecord(RECT_PROCESS, "proxy.process.cluster.ping_time_used", RECD_INT, &data, NULL); 
  data.rec_int = sum.send_delayed_time;
  RecSetRecord(RECT_PROCESS, "proxy.process.cluster.io.send_delayed_time", RECD_INT, &data, NULL); 
  data.rec_int = sum.push_msg_count;
  RecSetRecord(RECT_PROCESS, "proxy.process.cluster.io.push_msg_count", RECD_INT, &data, NULL); 
  data.rec_int = sum.push_msg_bytes;
  RecSetRecord(RECT_PROCESS, "proxy.process.cluster.io.push_msg_bytes", RECD_INT, &data, NULL); 

  RecDataSetFromInk64(RECD_INT, &nio_records.call_writev_count->data,
        sum.call_writev_count);
  RecDataSetFromInk64(RECD_INT, &nio_records.send_retry_count->data,
        sum.send_retry_count);
  RecDataSetFromInk64(RECD_INT, &nio_records.call_read_count->data,
        sum.call_read_count);
  RecDataSetFromInk64(RECD_INT, &nio_records.epoll_wait_count->data,
        sum.epoll_wait_count);
  RecDataSetFromInk64(RECD_INT, &nio_records.epoll_wait_time_used->data,
        sum.epoll_wait_time_used);
  RecDataSetFromInk64(RECD_INT, &nio_records.loop_usleep_count->data,
        sum.loop_usleep_count);
  RecDataSetFromInk64(RECD_INT, &nio_records.loop_usleep_time->data,
        sum.loop_usleep_time);

  RecDataSetFromInk64(RECD_INT, &nio_records.max_write_loop_time_used->data,
        max_write_loop_time_used);
  RecDataSetFromInk64(RECD_INT, &nio_records.max_read_loop_time_used->data,
        max_read_loop_time_used);
  RecDataSetFromInk64(RECD_INT, &nio_records.max_epoll_time_used->data,
        max_epoll_time_used);
  RecDataSetFromInk64(RECD_INT, &nio_records.max_usleep_time_used->data,
        max_usleep_time_used);
  RecDataSetFromInk64(RECD_INT, &nio_records.max_callback_time_used->data,
        max_callback_time_used);

  int time_pass = CURRENT_TIME() - last_calc_Bps_time;
  if (time_pass > 0) {
    double io_busy_ratio;
    int64_t nio_current_Bps = (sum.send_bytes - last_send_bytes) / time_pass;
    last_calc_Bps_time = CURRENT_TIME();
    last_send_bytes = sum.send_bytes;

    if (cluster_flow_ctrl_max_bps <= 0) {
      write_wait_time = cluster_send_min_wait_time * HRTIME_USECOND;
      io_loop_interval = cluster_min_loop_interval;
    }
    else {
      if (nio_current_Bps < cluster_flow_ctrl_min_bps) {
        write_wait_time = cluster_send_min_wait_time * HRTIME_USECOND;
        io_loop_interval = cluster_min_loop_interval;
      }
      else {
        io_busy_ratio = (double)nio_current_Bps / (double)cluster_flow_ctrl_max_bps;
        if (io_busy_ratio > 1.0) {
          io_busy_ratio = 1.0;
        }
        write_wait_time = (int)((cluster_send_min_wait_time +
              (cluster_send_max_wait_time - cluster_send_min_wait_time) *
              io_busy_ratio)) * HRTIME_USECOND;
        io_loop_interval = cluster_min_loop_interval + (int)((
              cluster_max_loop_interval - cluster_min_loop_interval) * io_busy_ratio);
      }
      RecDataSetFromInk64(RECD_INT, &nio_records.write_wait_time->data,
          write_wait_time / HRTIME_USECOND);
      RecDataSetFromInk64(RECD_INT, &nio_records.io_loop_interval->data,
          io_loop_interval);
    }
  }
}

int nio_init()
{
	int result;
	int bytes;
  int total_connections;
	int max_connections_per_thread;
	struct worker_thread_context *pThreadContext;
	struct worker_thread_context *pContextEnd;
	pthread_t tid;

	if ((result=init_pthread_lock(&worker_thread_lock)) != 0) {
		return result;
	}

	bytes = sizeof(struct worker_thread_context) * g_work_threads;
	g_worker_thread_contexts = (struct worker_thread_context *)malloc(bytes);
	if (g_worker_thread_contexts == NULL) {
		Error("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail, errno: %d, error info: %s", \
			__LINE__, bytes, errno, strerror(errno));
		return errno != 0 ? errno : ENOMEM;
	}
  memset(g_worker_thread_contexts, 0, bytes);

  total_connections = g_connections_per_machine * (MAX_MACHINE_COUNT - 1);
	max_connections_per_thread = total_connections / g_work_threads;
	if (total_connections % g_work_threads != 0) {
		max_connections_per_thread++;
	}

	g_worker_thread_count = 0;
	pContextEnd = g_worker_thread_contexts + g_work_threads;
	for (pThreadContext=g_worker_thread_contexts; pThreadContext<pContextEnd; pThreadContext++)
	{
		pThreadContext->thread_index = (int)(pThreadContext - g_worker_thread_contexts);
		pThreadContext->alloc_size = max_connections_per_thread;
		bytes = sizeof(struct epoll_event) * pThreadContext->alloc_size;
		pThreadContext->events = (struct epoll_event *)malloc(bytes);
		if (pThreadContext->events == NULL)
		{
			Error("file: "__FILE__", line: %d, " \
				"malloc %d bytes fail, errno: %d, error info: %s", \
				__LINE__, bytes, errno, strerror(errno));
			return errno != 0 ? errno : ENOMEM;
		}

		bytes = sizeof(SocketContext *) * pThreadContext->alloc_size;
    pThreadContext->active_sockets = (SocketContext **)malloc(bytes);
		if (pThreadContext->active_sockets == NULL)
		{
			Error("file: "__FILE__", line: %d, " \
				"malloc %d bytes fail, errno: %d, error info: %s", \
				__LINE__, bytes, errno, strerror(errno));
			return errno != 0 ? errno : ENOMEM;
		}

		pThreadContext->epoll_fd = epoll_create(pThreadContext->alloc_size);
		if (pThreadContext->epoll_fd < 0)
		{
			Error("file: " __FILE__ ", line: %d, "
				"poll_create fail, errno: %d, error info: %s", \
				__LINE__, errno, strerror(errno));
			return errno != 0 ? errno : ENOMEM;
		}

		if ((result=init_pthread_lock(&pThreadContext->lock)) != 0)
		{
			return result;
		}

		if ((result=pthread_create(&tid, NULL,
			work_thread_entrance, pThreadContext)) != 0)
		{
			Error("file: "__FILE__", line: %d, " \
				"create thread failed, startup threads: %d, " \
				"errno: %d, error info: %s",
				__LINE__, g_worker_thread_count,
				result, strerror(result));
			break;
		}
		else
		{
			if ((result=pthread_mutex_lock(&worker_thread_lock)) != 0) {
				Error("file: "__FILE__", line: %d, " \
					"call pthread_mutex_lock fail, " \
					"errno: %d, error info: %s",
					__LINE__, result, strerror(result));
			}
			g_worker_thread_count++;
			if ((result=pthread_mutex_unlock(&worker_thread_lock)) != 0) {
				Error("file: "__FILE__", line: %d, " \
					"call pthread_mutex_unlock fail, " \
					"errno: %d, error info: %s",
					__LINE__, result, strerror(result));
			}
		}
	}

  init_nio_stats();

	//int2buff(MAGIC_NUMBER, magic_buff);
	return 0;
}

int nio_destroy()
{
	pthread_mutex_destroy(&worker_thread_lock);
	return 0;
}

int cluster_global_init(message_deal_func deal_func,
    machine_change_notify_func machine_change_notify)
{
  g_msg_deal_func = deal_func;
  g_machine_change_notify = machine_change_notify;
  return 0;
}

#define ALLOC_READER_BUFFER(reader, len) \
  do { \
    reader.buffer = new_RecvBuffer(len); \
    reader.current = reader.buffer->_data; \
    reader.buff_end = reader.buffer->_data + len; \
  } while (0)

#define INIT_READER(reader, len) \
  do { \
    reader.buffer = new_RecvBuffer(len); \
    reader.current = reader.msg_header = reader.buffer->_data; \
    reader.buff_end = reader.msg_header + len; \
  } while (0)

#define MOVE_TO_NEW_BUFFER(pSockContext, msg_bytes) \
  do { \
    Ptr<IOBufferData> oldBuffer; \
    char *old_msg_header; \
    oldBuffer = pSockContext->reader.buffer; \
    old_msg_header = pSockContext->reader.msg_header; \
    INIT_READER(pSockContext->reader, READ_BUFFER_SIZE); \
    memcpy(pSockContext->reader.current, old_msg_header, msg_bytes); \
    pSockContext->reader.current += msg_bytes; \
    oldBuffer = NULL; \
  } while (0)


static int set_socket_rw_buff_size(int sock)
{
	int bytes;

  if (g_socket_send_bufsize > 0) {
    bytes = g_socket_send_bufsize;
    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
          (char *)&bytes, sizeof(int)) < 0)
    {
      Error("file: "__FILE__", line: %d, " \
          "setsockopt failed, errno: %d, error info: %s",
          __LINE__, errno, strerror(errno));
      return errno != 0 ? errno : ENOMEM;
    }
  }

  if (g_socket_recv_bufsize > 0) {
    bytes = g_socket_recv_bufsize;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
          (char *)&bytes, sizeof(int)) < 0)
    {
      Error("file: "__FILE__", line: %d, " \
          "setsockopt failed, errno: %d, error info: %s",
          __LINE__, errno, strerror(errno));
      return errno != 0 ? errno : ENOMEM;
    }
  }

  return 0;
}

static int add_to_active_sockets(SocketContext *pSockContext)
{
  pthread_mutex_lock(&pSockContext->thread_context->lock);
  pSockContext->thread_context->active_sockets[
    pSockContext->thread_context->active_sock_count] = pSockContext;
  pSockContext->thread_context->active_sock_count++;
  pthread_mutex_unlock(&pSockContext->thread_context->lock);
  return 0;
}

static int remove_from_active_sockets(SocketContext *pSockContext)
{
  int result;
  SocketContext **ppSockContext;
  SocketContext **ppContextEnd;
  SocketContext **ppCurrent;

  pthread_mutex_lock(&pSockContext->thread_context->lock);
  ppContextEnd = pSockContext->thread_context->active_sockets +
    pSockContext->thread_context->active_sock_count;
  for (ppSockContext=pSockContext->thread_context->active_sockets;
      ppSockContext<ppContextEnd; ppSockContext++)
  {
    if (*ppSockContext == pSockContext) {
      break;
    }
  }

  if (ppSockContext == ppContextEnd) {
      Error("file: "__FILE__", line: %d, " \
          "socket context for %s not found!", __LINE__,
          pSockContext->machine->hostname);
    result = ENOENT;
  }
  else {
    for (ppCurrent=ppSockContext+1; ppCurrent<ppContextEnd; ppCurrent++) {
      *(ppCurrent - 1) = *ppCurrent;
    }
    pSockContext->thread_context->active_sock_count--;
    result = 0;
  }
  pthread_mutex_unlock(&pSockContext->thread_context->lock);

  return result;
}

int nio_add_to_epoll(SocketContext *pSockContext)
{
	struct epoll_event event;
  int i;

  /*
  Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
      "%s:%d nio_add_to_epoll", __LINE__, pSockContext->machine->hostname,
      pSockContext->machine->cluster_port);
  */

  pSockContext->connected_time = CURRENT_TIME();
  for (i=0; i<PRIORITY_COUNT; i++) {
    pSockContext->send_queues[i].head = NULL;
    pSockContext->send_queues[i].tail = NULL;
  }
  pSockContext->queue_index = 0;
  pSockContext->ping_start_time = 0;
  pSockContext->ping_fail_count = 0;
  pSockContext->next_write_time = CURRENT_NS() + write_wait_time;
  pSockContext->next_ping_time = CURRENT_NS() + cluster_ping_send_interval;

  INIT_READER(pSockContext->reader, READ_BUFFER_SIZE);
  pSockContext->reader.recv_body_bytes = 0;

  set_socket_rw_buff_size(pSockContext->sock);
  init_machine_sessions(pSockContext->machine, false);
  add_machine_sock_context(pSockContext);

	event.data.ptr = pSockContext;
	event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
	if (epoll_ctl(pSockContext->thread_context->epoll_fd, EPOLL_CTL_ADD,
		pSockContext->sock, &event) != 0)
	{
		Error("file: " __FILE__ ", line: %d, "
			"epoll_ctl fail, errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
    remove_machine_sock_context(pSockContext);  //rollback
		return errno != 0 ? errno : ENOMEM;
	}

	return add_to_active_sockets(pSockContext);
}

/*
static void pack_header(OutMessage *msg, char *buff)
{
	buff[0] = magic_buff[0];
	buff[1] = magic_buff[1];
	buff[2] = magic_buff[2];
	buff[3] = magic_buff[3];

	int2buff(msg->header.func_id, buff + 4);
	int2buff(msg->header.data_len, buff + 8);
	long2buff(msg->header.session_id.id, buff + 12);
}
*/

static void clear_send_queue(SocketContext * pSockContext)
{
  int i;
  int count;
	OutMessage *msg;
  MessageQueue *send_queue;

  count = 0;
  for (i=0; i<PRIORITY_COUNT; i++) {
    send_queue = pSockContext->send_queues + i;
    pthread_mutex_lock(&send_queue->lock);
    while (send_queue->head != NULL) {
      msg = send_queue->head;
      send_queue->head = send_queue->head->next;
      release_out_message(pSockContext, msg);
      count++;
    }
    pthread_mutex_unlock(&send_queue->lock);
  }

  if (count > 0) {
    Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
        "release %s:%d #%d message count: %d",
        __LINE__, pSockContext->machine->hostname,
        pSockContext->machine->cluster_port, pSockContext->sock, count);
  }
}

static int close_socket(SocketContext * pSockContext)
{
	if (epoll_ctl(pSockContext->thread_context->epoll_fd, EPOLL_CTL_DEL,
		pSockContext->sock, NULL) != 0)
	{
		Error("file: " __FILE__ ", line: %d, "
			"epoll_ctl fail, errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
		return errno != 0 ? errno : ENOMEM;
	}
  remove_from_active_sockets(pSockContext);

  machine_remove_connection(pSockContext);
	close(pSockContext->sock);
  pSockContext->sock = -1;

  pSockContext->reader.blocks = NULL;
  pSockContext->reader.buffer = NULL;

  clear_send_queue(pSockContext);
  notify_connection_closed(pSockContext);

  if (pSockContext->connect_type == CONNECT_TYPE_CLIENT) {
    make_connection(pSockContext);
  }
  else {
    free_accept_sock_context(pSockContext);
  }
  
	return 0;
}

inline static int send_ping_message(SocketContext *pSockContext)
{
  ClusterSession session;

  //ping message do NOT care session id
  session.fields.ip = g_my_machine_ip;
  session.fields.timestamp = CURRENT_TIME();
  session.fields.seq = 0;   //just use 0
  return cluster_send_msg_internal_ex(&session,
      pSockContext, FUNC_ID_CLUSTER_PING_REQUEST, NULL, 0, PRIORITY_HIGH,
      insert_into_send_queue_head);
}

static int deal_write_event(SocketContext * pSockContext)
{
#define BUFF_TYPE_HEADER    'H'
#define BUFF_TYPE_DATA      'D'
#define BUFF_TYPE_PADDING   'P'

  MessageQueue *send_queue;
	struct iovec write_vec[WRITEV_ARRAY_SIZE];
  struct {
    int priority;
    int index;     //message index
    int  buff_type;  //message data or header
  } msg_indexes[WRITEV_ARRAY_SIZE];

  struct {
    OutMessage *send_msgs[WRITEV_ITEM_ONCE];
    OutMessage *done_msgs[WRITEV_ITEM_ONCE];
    OutMessage **pDoneMsgs;
    int msg_count;
    int done_count;
  } msgs[PRIORITY_COUNT];

	OutMessage *msg;
  int write_bytes;
  int remain_len;
  int priority;
  int start;
  int total_msg_count;
	int vec_count;
	int total_bytes;
  int total_done_count;
	int result;
  int i, k;
  bool fetch_done;
  bool last_msg_complete;

	msgs[0].msg_count = msgs[1].msg_count = msgs[2].msg_count = 0;
  total_msg_count = 0;
	vec_count = 0;
	total_bytes = 0;

  priority = pSockContext->queue_index;
  if (pSockContext->queue_index == 0) {
    start = 1;  //only loop 3 times
  }
  else {
    start = 0;  //need loop 4 times
  }

  last_msg_complete = false;
  fetch_done = false;
  for (i=start; i<=PRIORITY_COUNT; i++) {
    send_queue = pSockContext->send_queues + priority;
    pthread_mutex_lock(&send_queue->lock);
    msg = send_queue->head;
    if (pSockContext->queue_index > 0 &&
        i == pSockContext->queue_index + 1)
    {
      if (msg != NULL) {
        msg = msg->next;  //should skip to next for the first already consumed
      }
    }
    while (msg != NULL) {
      if (msg->bytes_sent < MSG_HEADER_LENGTH) {  //should send header
        write_vec[vec_count].iov_base = ((char *)&msg->header) +
          msg->bytes_sent;
        write_vec[vec_count].iov_len = MSG_HEADER_LENGTH -
          msg->bytes_sent;
        total_bytes += write_vec[vec_count].iov_len;
        msg_indexes[vec_count].priority = priority;
        msg_indexes[vec_count].buff_type = BUFF_TYPE_HEADER;
        msg_indexes[vec_count].index = msgs[priority].msg_count;
        vec_count++;

        remain_len = msg->header.aligned_data_len;
      }
      else {
        remain_len = (msg->header.aligned_data_len + MSG_HEADER_LENGTH) -
          msg->bytes_sent;
      }

      if (remain_len > 0) {
        int pad_len;
        int remain_data_len;
        pad_len = msg->header.aligned_data_len - msg->header.data_len;
        remain_data_len = remain_len - pad_len;
        if (remain_data_len > 0) {
          if (msg->data_type == DATA_TYPE_OBJECT) {
            int read_count;
            int64_t read_bytes;

            read_count = get_iovec(msg->blocks, write_vec + vec_count,
                WRITEV_ARRAY_SIZE - 1 -  vec_count);
            read_bytes = 0;
            for (k=0; k<read_count; k++) {
              read_bytes += write_vec[vec_count].iov_len;
              msg_indexes[vec_count].priority = priority;
              msg_indexes[vec_count].buff_type = BUFF_TYPE_DATA;
              msg_indexes[vec_count].index = msgs[priority].msg_count;
              vec_count++;
            }
            //assert(read_bytes <= remain_data_len);

            total_bytes += read_bytes;
            last_msg_complete = read_bytes == remain_data_len;
          }
          else {
            write_vec[vec_count].iov_base = msg->mini_buff +
              (msg->header.data_len - remain_data_len);
            write_vec[vec_count].iov_len = remain_data_len;
            total_bytes += write_vec[vec_count].iov_len;
            msg_indexes[vec_count].priority = priority;
            msg_indexes[vec_count].buff_type = BUFF_TYPE_DATA;
            msg_indexes[vec_count].index = msgs[priority].msg_count;
            vec_count++;
            last_msg_complete = true;
          }
        }
        else {  //no more data
          last_msg_complete = true;
        }

        if (pad_len > 0 && last_msg_complete) {
          write_vec[vec_count].iov_base = pSockContext->padding;
          write_vec[vec_count].iov_len = (remain_data_len > 0) ?
            pad_len : remain_len;
          total_bytes += write_vec[vec_count].iov_len;
          msg_indexes[vec_count].priority = priority;
          msg_indexes[vec_count].buff_type = BUFF_TYPE_PADDING;
          msg_indexes[vec_count].index = msgs[priority].msg_count;
          vec_count++;
        }
      }
      else {
        last_msg_complete = true;
      }

      msgs[priority].send_msgs[msgs[priority].msg_count++] = msg;
      total_msg_count++;

      /*
      Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
          "%s:%d sending msg, data body: %d, msg send bytes: %d, total_bytes: %d",
          __LINE__,
          pSockContext->machine->hostname,
          pSockContext->machine->cluster_port,
          msg->header.data_len,
          msg->bytes_sent, total_bytes);
      */
      if (total_msg_count == WRITEV_ITEM_ONCE ||
          vec_count >= WRITEV_ARRAY_SIZE - 2 ||
          total_bytes >= WRITE_MAX_COMBINE_BYTES)
      {
        fetch_done = true;
        break;
      }
      if (i == 0) {  //fetch only one, the head message
        break;
      }
      msg = msg->next;
    }
    pthread_mutex_unlock(&send_queue->lock);

    if (fetch_done) {
      break;
    }

    if (i == 0) {
      priority = 0;  //next should start from first priority
    }
    else {
      priority++;
    }
  }

  /*
  Debug(CLUSTER_DEBUG_TAG, "==wwwwww==file: " __FILE__ ", line: %d, "
      "%s:%d total_bytes: %d, vec_count: %d, total_msg_count: %d", __LINE__,
      pSockContext->machine->hostname,
      pSockContext->machine->cluster_port,
      total_bytes, vec_count, total_msg_count);
  */

	if (vec_count == 0) {
    return EAGAIN;
	}

  pSockContext->thread_context->stats.send_retry_count += total_msg_count;
  pSockContext->thread_context->stats.call_writev_count++;
	write_bytes = writev(pSockContext->sock, write_vec, vec_count);
	if (write_bytes == 0) {   //connection closed
		Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, "
			"write to %s fail, connection closed",
			__LINE__, pSockContext->machine->hostname);
		return ECONNRESET;
	}
	else if (write_bytes < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return EAGAIN;
		}
    else if (errno == EINTR) {  //should try again
			Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__ ", line: %d, "
				"write to %s fail, errno: %d, error info: %s",
				__LINE__, pSockContext->machine->hostname,
        errno, strerror(errno));
      return 0;
    }
		else {
			result = errno != 0 ? errno : EIO;
      Error("file: "__FILE__", line: %d, "
				"write to %s fail, errno: %d, error info: %s",
				__LINE__, pSockContext->machine->hostname,
        result, strerror(result));
			return result;
		}
	}

  pSockContext->thread_context->stats.send_bytes += write_bytes;
  if (write_bytes == total_bytes) {
    result = 0;
  }
  else {
    result = EAGAIN;
  }

	if (write_bytes == total_bytes && last_msg_complete) {  //all done
    for (i=0; i<PRIORITY_COUNT; i++) {
      msgs[i].pDoneMsgs = msgs[i].send_msgs;
      msgs[i].done_count = msgs[i].msg_count;
    }

    total_done_count = total_msg_count;
    pSockContext->queue_index = 0;
	}
  else {
    int vi;
    int remain_bytes;
    int done_index;

    for (i=0; i<PRIORITY_COUNT; i++) {
      msgs[i].pDoneMsgs = msgs[i].done_msgs;
      msgs[i].done_count = 0;
    }
    total_done_count = 0;

    remain_bytes = write_bytes;
    for (vi=0; vi<vec_count; vi++) {
      remain_bytes -= write_vec[vi].iov_len;
      msg = msgs[msg_indexes[vi].priority].send_msgs[msg_indexes[vi].index];

      if (remain_bytes >= 0) {
        if (msg->data_type == DATA_TYPE_OBJECT &&
            msg_indexes[vi].buff_type == BUFF_TYPE_DATA)
        {
          consume(msg, write_vec[vi].iov_len);
        }
        msg->bytes_sent += write_vec[vi].iov_len;

        if (msg->bytes_sent >= MSG_HEADER_LENGTH + msg->header.aligned_data_len) {
          total_done_count++;
          done_index = msgs[msg_indexes[vi].priority].done_count++;
          msgs[msg_indexes[vi].priority].done_msgs[done_index] = msg;
        }
      }
      else {
        if (msg->data_type == DATA_TYPE_OBJECT &&
            msg_indexes[vi].buff_type == BUFF_TYPE_DATA)
        {
          consume(msg, remain_bytes + write_vec[vi].iov_len);
        }
        msg->bytes_sent += remain_bytes + write_vec[vi].iov_len;

        break;
      }
    }

    if (vi < vec_count) {
      pSockContext->queue_index = msg_indexes[vi].priority;  //the first not done msg
    }
    else {
      pSockContext->queue_index = msg_indexes[vi - 1].priority;  //the first not done msg
    }

    if (total_done_count == 0) {
      return result;
    }
  }
  pSockContext->thread_context->stats.send_msg_count += total_done_count;

  for (i=0; i<PRIORITY_COUNT; i++) {
    if (msgs[i].done_count == 0) {
      continue;
    }

    send_queue = pSockContext->send_queues + i;
    pthread_mutex_lock(&send_queue->lock);
    send_queue->head = msgs[i].pDoneMsgs[msgs[i].done_count - 1]->next;
    if (send_queue->head == NULL) {
      send_queue->tail = NULL;
    }
    pthread_mutex_unlock(&send_queue->lock);
  }

  for (i=0; i<PRIORITY_COUNT; i++) {
    for (k=0; k<msgs[i].done_count; k++) {
      msg = msgs[i].pDoneMsgs[k];
#ifdef MSG_TIME_STAT_FLAG
      MachineSessions *pMachineSessions;
      SessionEntry *pSessionEntry;
      if (get_response_session_internal(&msg->header,
            &pMachineSessions, &pSessionEntry) == 0)
      {
        int session_index = msg->header.session_id.fields.seq %
          MAX_SESSION_COUNT_PER_MACHINE;
        SESSION_LOCK(pMachineSessions, session_index);

        if (!(msg->header.session_id.fields.ip == g_my_machine_ip))
        {  //request by other
          if (pSessionEntry->server_start_time != 0) {
            __sync_fetch_and_add(&pMachineSessions->msg_stat.count, 1);
            __sync_fetch_and_add(&pMachineSessions->msg_stat.time_used,
                CURRENT_NS() - pSessionEntry->server_start_time);
            pSessionEntry->server_start_time = 0;
          }
        }

        if (pSessionEntry->send_start_time != 0) {
          __sync_fetch_and_add(&pMachineSessions->msg_send.count, 1);
          __sync_fetch_and_add(&pMachineSessions->msg_send.time_used,
              (CURRENT_NS() - pSessionEntry->send_start_time));
          pSessionEntry->send_start_time = 0;
        }

        SESSION_UNLOCK(pMachineSessions, session_index);
      }
#endif


      /*
      Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
          "%s:%d send msg done, data body: %d, send bytes: %d",
          __LINE__,
          pSockContext->machine->hostname,
          pSockContext->machine->cluster_port,
          msgs[i].pDoneMsgs[k]->header.data_len,
          msgs[i].pDoneMsgs[k]->bytes_sent);
      */

      pSockContext->thread_context->stats.send_delayed_time +=
        CURRENT_NS() - msg->in_queue_time;
      release_out_message(pSockContext, msg);
    }
  }

  return result;
}

static int deal_message(MsgHeader *pHeader, SocketContext *
    pSockContext, IOBufferBlock *blocks)
{
  int result;
  bool call_func;
  MachineSessions *pMachineSessions;
  SessionEntry *pSessionEntry;
  void *user_data;
  int64_t time_used;

 /*
  Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
      "func_id: %d, data length: %d, recv_msg_count: %ld", __LINE__,
      pHeader->func_id, data_len, count + 1);
  */

  //deal internal ping message first
  if (pHeader->func_id == FUNC_ID_CLUSTER_PING_REQUEST) {
      time_used = CURRENT_TIME() - pHeader->session_id.fields.timestamp;
      if (time_used > 1) {
        Warning("cluster recv client %s ping, sock: #%d, time pass: %d s",
            pSockContext->machine->hostname, pSockContext->sock,
            (int)time_used);
      }
      return cluster_send_msg_internal_ex(&pHeader->session_id,
          pSockContext, FUNC_ID_CLUSTER_PING_RESPONSE, NULL, 0,
          PRIORITY_HIGH, insert_into_send_queue_head);
  }
  else if (pHeader->func_id == FUNC_ID_CLUSTER_PING_RESPONSE) {
    if (pSockContext->ping_start_time > 0) {
      time_used = CURRENT_NS() - pSockContext->ping_start_time;
      pSockContext->thread_context->stats.ping_success_count++;
      pSockContext->thread_context->stats.ping_time_used += time_used;
      if (time_used > cluster_ping_latency_threshold) {
        Warning("cluster server %s, sock: #%d ping response time: %d ms > threshold: %d ms",
            pSockContext->machine->hostname, pSockContext->sock,
            (int)(time_used / HRTIME_MSECOND),
            (int)(cluster_ping_latency_threshold / HRTIME_MSECOND));
      }
      else if (pSockContext->ping_fail_count > 0) {
        pSockContext->ping_fail_count = 0;  //reset fail count
      }
      pSockContext->ping_start_time = 0;  //reset start time
    }
    else {
      Warning("unexpect cluster server %s ping response, sock: #%d, time used: %d s",
          pSockContext->machine->hostname, pSockContext->sock,
          (int)(CURRENT_TIME() - pHeader->session_id.fields.timestamp));
    }

    return 0;
  }

  result = get_response_session(pHeader, &pMachineSessions,
      &pSessionEntry, pSockContext, &call_func, &user_data);
  if (result != 0) {
    /*
    if (pHeader->session_id.fields.ip != g_my_machine_ip) {  //request by other
      cluster_send_msg_internal_ex(&pHeader->session_id, pSockContext,
          FUNC_ID_CONNECTION_CLOSED_NOTIFY, NULL, 0, PRIORITY_HIGH,
          push_to_send_queue);
    }
    */

    return result;
  }

#ifdef MSG_TIME_STAT_FLAG
  if ((pHeader->session_id.fields.ip == g_my_machine_ip)) {  //request by me
    int session_index = pHeader->session_id.fields.seq %
      MAX_SESSION_COUNT_PER_MACHINE;
    SESSION_LOCK(pMachineSessions, session_index);
    if (pSessionEntry->client_start_time != 0) {
      __sync_fetch_and_add(&pMachineSessions->msg_stat.count, 1);
      __sync_fetch_and_add(&pMachineSessions->msg_stat.time_used,
        CURRENT_NS() - pSessionEntry->client_start_time);
      pSessionEntry->client_start_time = 0;
    }
    SESSION_UNLOCK(pMachineSessions, session_index);
  }
#endif
 
  if (call_func) {
    int64_t deal_start_time = CURRENT_NS();
    g_msg_deal_func(pHeader->session_id, user_data,
        pHeader->func_id, blocks, pHeader->data_len);
    int64_t time_used = CURRENT_NS() - deal_start_time;
    if (time_used > max_callback_time_used) {
      max_callback_time_used = time_used;
    }
  }
  else {
    push_in_message(pHeader->session_id, pMachineSessions, pSessionEntry,
        pHeader->func_id, blocks, pHeader->data_len);
  }

  return 0;
}

inline static void append_to_blocks(ReaderManager *pReader,
    const int current_body_bytes)
{
  IOBufferBlock *b;
  IOBufferBlock *tail;

  if (pReader->blocks == NULL) {  //first block
    pReader->blocks = new_IOBufferBlock(
        pReader->buffer, current_body_bytes,
        (pReader->msg_header + MSG_HEADER_LENGTH)
        - pReader->buffer->_data);
    pReader->blocks->_buf_end = pReader->blocks->_end;
    return;
  }

  //other block, starting from buffer start
  b = new_IOBufferBlock(pReader->buffer, current_body_bytes, 0);
  b->_buf_end = b->_end;
  if (pReader->blocks->next == NULL) {
    pReader->blocks->next = b;
    return;
  }

  tail = pReader->blocks->next;
  while (tail->next != NULL) {
    tail = tail->next;
  }

  tail->next = b;
}

static int deal_read_event(SocketContext *pSockContext)
{
  int result;
  int read_bytes;
  MsgHeader *pHeader;

  pSockContext->thread_context->stats.call_read_count++;
  read_bytes = read(pSockContext->sock, pSockContext->reader.current,
      pSockContext->reader.buff_end - pSockContext->reader.current);
  /*
  Note("======file: " __FILE__ ", line: %d, "
      "sock: #%d, %s:%d remain bytes: %ld, recv bytes: %d, errno: %d", __LINE__,
      pSockContext->sock, pSockContext->machine->hostname,
      pSockContext->machine->cluster_port,
      pSockContext->reader.buff_end - pSockContext->reader.current,
      read_bytes, errno);
  */
	if (read_bytes == 0) {
     Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
          "type: %c, read from %s fail, connection #%d closed", __LINE__,
          pSockContext->connect_type, pSockContext->machine->hostname,
          pSockContext->sock);
      return ECONNRESET;
	}
	else if (read_bytes < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return EAGAIN;
		}
    else if (errno == EINTR) {  //should try again
			Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
				"read from %s fail, errno: %d, error info: %s",
				__LINE__, pSockContext->machine->hostname,
        errno, strerror(errno));
      return 0;
    }
		else {
			result = errno != 0 ? errno : EIO;
			Error("file: " __FILE__ ", line: %d, "
				"read from %s fail, errno: %d, error info: %s",
				__LINE__, pSockContext->machine->hostname,
        result, strerror(result));
			return result;
		}
	}

  pSockContext->thread_context->stats.recv_bytes += read_bytes;
  pSockContext->reader.current += read_bytes;
  result = pSockContext->reader.buff_end - pSockContext->reader.current
    == 0 ? 0 : EAGAIN;

  //current is the fix buffer
  while (1) {
    int msg_bytes;
    int recv_body_bytes;
    int current_true_body_bytes;
    int padding_body_bytes;
    int padding_len;
    bool bFirstBlock;

    if (pSockContext->reader.blocks == NULL) { //first data block
      msg_bytes = pSockContext->reader.current -
        pSockContext->reader.msg_header;
      if (msg_bytes < MSG_HEADER_LENGTH) //expect whole msg header
      {
        if ((pSockContext->reader.buff_end -
              pSockContext->reader.current) < 4 * 1024)
        {
          if (msg_bytes > 0) {  //remain bytes should be copied
            MOVE_TO_NEW_BUFFER(pSockContext, msg_bytes);
          }
          else {
            INIT_READER(pSockContext->reader, READ_BUFFER_SIZE);
          }
        }

        return result;
      }

      recv_body_bytes  = msg_bytes - MSG_HEADER_LENGTH;
      bFirstBlock = true;
    }
    else {   //other data block, starting from buffer start
      msg_bytes = pSockContext->reader.current -
        pSockContext->reader.buffer->_data;
      recv_body_bytes = pSockContext->reader.recv_body_bytes + msg_bytes;
      bFirstBlock = false;
    }

    pHeader = (MsgHeader *)pSockContext->reader.msg_header;
#ifdef CHECK_MAGIC_NUMBER
    if (pHeader->magic != MAGIC_NUMBER) {
			Error("file: "__FILE__", line: %d, " \
				"magic number: %08x != %08x", \
				__LINE__, pHeader->magic, MAGIC_NUMBER);
      return EINVAL;
    }
#endif

    if (pHeader->aligned_data_len > MAX_MSG_LENGTH) {
			Error("file: "__FILE__", line: %d, " \
				"message length: %d is too large, exceeds: %d", \
				__LINE__, pHeader->aligned_data_len, MAX_MSG_LENGTH);
      return ENOSPC;
    }

#ifdef MSG_TIME_STAT_FLAG
      if (!(pHeader->session_id.fields.ip == g_my_machine_ip))
      {  //request by other
        MachineSessions *pMachineSessions;
        SessionEntry *pSessionEntry;
        if (get_response_session_internal(pHeader,
              &pMachineSessions, &pSessionEntry) == 0)
        {
          int session_index = pHeader->session_id.fields.seq %
            MAX_SESSION_COUNT_PER_MACHINE;
          SESSION_LOCK(pMachineSessions, session_index);
          if (pSessionEntry->server_start_time == 0) {
            pSessionEntry->server_start_time = CURRENT_NS();
          }
          SESSION_UNLOCK(pMachineSessions, session_index);
        }
      }
#endif

    if (recv_body_bytes < pHeader->aligned_data_len) {  //msg not done
      if (recv_body_bytes + (pSockContext->reader.buff_end - 
            pSockContext->reader.current) >= pHeader->aligned_data_len)
      {  //remain buffer is enough
        return result;
      }

      padding_body_bytes = recv_body_bytes - pSockContext->
        reader.recv_body_bytes;
      int recv_padding_len = recv_body_bytes - pHeader->data_len;
      if (recv_padding_len > 0) {  //should remove padding bytes
        current_true_body_bytes = padding_body_bytes - recv_padding_len;
      }
      else {
        current_true_body_bytes = padding_body_bytes;
      }

      //must be only one block
      if (pHeader->func_id < 0) {
        if (!bFirstBlock) {
          Error("file: "__FILE__", line: %d, " \
              "func_id: %d, data length: %d too large exceeds %d",
              __LINE__, pHeader->func_id, pHeader->data_len,
              (int)(READ_BUFFER_SIZE - MSG_HEADER_LENGTH));
          return EINVAL;
        }

        MOVE_TO_NEW_BUFFER(pSockContext, msg_bytes);
        return result;
      }

      if (pSockContext->reader.buff_end - pSockContext->reader.current >=
          4 * 1024)
      { //use remain data buffer
        return result;
      }

      if (recv_body_bytes % ALIGN_BYTES != 0) { //must be aligned
        Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
            "recv_body_bytes: %d (%X) should be aligned with %d", __LINE__,
            recv_body_bytes, recv_body_bytes, ALIGN_BYTES);
        ink_release_assert(pSockContext->reader.current < pSockContext->reader.buff_end);
        return result;
      }

      if (current_true_body_bytes > 0) { //should alloc new buffer
        append_to_blocks(&pSockContext->reader, current_true_body_bytes);
      }
      pSockContext->reader.recv_body_bytes = recv_body_bytes;

      if (bFirstBlock) {
        if (current_true_body_bytes > 0) {  //should keep the msg_header
          ALLOC_READER_BUFFER(pSockContext->reader, READ_BUFFER_SIZE);
        }
        else { //no data yet!
          MOVE_TO_NEW_BUFFER(pSockContext, msg_bytes);
        }
      }
      else {  //should keep the msg_header
        ALLOC_READER_BUFFER(pSockContext->reader, READ_BUFFER_SIZE);
      }

      return result;
    }

    if (bFirstBlock) {
      padding_body_bytes = pHeader->aligned_data_len;
    }
    else {
      padding_body_bytes = pHeader->aligned_data_len -
        pSockContext->reader.recv_body_bytes;
    }
    padding_len = pHeader->aligned_data_len - pHeader->data_len;
    if (padding_len > 0) {
      if (padding_body_bytes > padding_len) {
        current_true_body_bytes = padding_body_bytes - padding_len;
      }
      else {
        current_true_body_bytes = 0;
      }
    }
    else {  //no padding bytes
      current_true_body_bytes = padding_body_bytes;
    }

    if (current_true_body_bytes > 0) {
      append_to_blocks(&pSockContext->reader, current_true_body_bytes);
    }

    pSockContext->thread_context->stats.recv_msg_count++;
    deal_message(pHeader, pSockContext, pSockContext->reader.blocks);

    pSockContext->reader.blocks = NULL;  //free memory pointer
    if (pSockContext->reader.recv_body_bytes > 0) {
      pSockContext->reader.recv_body_bytes = 0;
    }

    if (bFirstBlock) {
      pSockContext->reader.msg_header += MSG_HEADER_LENGTH + padding_body_bytes;
    }
    else {  //other block, no msg header
      pSockContext->reader.msg_header = pSockContext->reader.buffer->_data +
        padding_body_bytes;
    }
  }

	return result;
}

inline static void deal_epoll_events(struct worker_thread_context *
	pThreadContext, const int count)
{
  int result;
	struct epoll_event *pEvent;
	struct epoll_event *pEventEnd;
  SocketContext *pSockContext;

	pEventEnd = pThreadContext->events + count;
	for (pEvent=pThreadContext->events; pEvent<pEventEnd; pEvent++) {
	  pSockContext = (SocketContext *)pEvent->data.ptr;

    /*
    Debug(CLUSTER_DEBUG_TAG, "======file: "__FILE__", line: %d, " \
        "sock #%d get epoll event: %d", __LINE__,
        pSockContext->sock, pEvent->events);
    */

    if ((pEvent->events & EPOLLRDHUP) || (pEvent->events & EPOLLERR) ||
        (pEvent->events & EPOLLHUP))
    {
      Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
          "connection %s %s:%d closed", __LINE__,
          pSockContext->connect_type == CONNECT_TYPE_CLIENT ? "to" : "from",
          pSockContext->machine->hostname, pSockContext->machine->cluster_port);

      close_socket(pSockContext);
      continue;
    }

    while ((result=deal_read_event(pSockContext)) == 0) {
    }

    if (result != EAGAIN) {
      close_socket(pSockContext);
    }
  }

	return;
}

inline static void schedule_sock_write(struct worker_thread_context * pThreadContext)
{
#define MAX_SOCK_CONTEXT_COUNT 32
  int result;
  int fail_count;
  int64_t current_time;
  SocketContext **ppSockContext;
  SocketContext **ppContextEnd;
  SocketContext *failSockContexts[MAX_SOCK_CONTEXT_COUNT];

  fail_count = 0;
  current_time = CURRENT_NS();
  ppContextEnd = pThreadContext->active_sockets +
    pThreadContext->active_sock_count;
  for (ppSockContext = pThreadContext->active_sockets;
      ppSockContext < ppContextEnd; ppSockContext++)
  {
    if (current_time < (*ppSockContext)->next_write_time) {
      continue;
    }

    if ((*ppSockContext)->ping_start_time > 0) { //ping message already sent
      if (current_time - (*ppSockContext)->ping_start_time > cluster_ping_latency_threshold) {
        (*ppSockContext)->ping_start_time = 0;  //reset start time when done
        (*ppSockContext)->ping_fail_count++;
        if ((*ppSockContext)->ping_fail_count > cluster_ping_retries) {
          if (fail_count < MAX_SOCK_CONTEXT_COUNT) {
            Error("ping cluster server %s timeout more than %d times, close socket #%d",
                (*ppSockContext)->machine->hostname, cluster_ping_retries,
                (*ppSockContext)->sock);
            failSockContexts[fail_count++] = *ppSockContext;
          }
          continue;
        }
        else {
          Warning("ping cluster server %s timeout, sock: #%d, fail count: %d",
              (*ppSockContext)->machine->hostname, (*ppSockContext)->sock,
              (*ppSockContext)->ping_fail_count);
        }
      }
    }
    else {
      if (cluster_ping_send_interval > 0 && current_time >=
          (*ppSockContext)->next_ping_time)
      {
        (*ppSockContext)->thread_context->stats.ping_total_count++;
        (*ppSockContext)->ping_start_time = current_time;
        (*ppSockContext)->next_ping_time = current_time + cluster_ping_send_interval;
        send_ping_message(*ppSockContext);
      }
    }

    while ((result=deal_write_event(*ppSockContext)) == 0) {
    }

    if (result == EAGAIN) {
      (*ppSockContext)->next_write_time = current_time + write_wait_time;
    }
    else {  //error
      if (fail_count < MAX_SOCK_CONTEXT_COUNT) {
        failSockContexts[fail_count++] = *ppSockContext;
      }
    }
  }

  if (fail_count == 0) {
    return;
  }

  ppContextEnd = failSockContexts + fail_count;
  for (ppSockContext = failSockContexts; ppSockContext < ppContextEnd;
      ppSockContext++)
  {
    close_socket(*ppSockContext);
  }
}

inline static int64_t get_current_time()
{
  timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * HRTIME_SECOND +
    tv.tv_usec * HRTIME_USECOND;
}

#define GET_MAX_TIME_USED(v) \
  do { \
    deal_end_time = get_current_time(); \
    time_used = deal_end_time - deal_start_time; \
    if (time_used > v) { \
      v = time_used; \
    } \
    deal_start_time = deal_end_time; \
  } while (0)


static void *work_thread_entrance(void* arg)
{
#define MIN_USLEEP_TIME 100

	int result;
	int count;
  int remain_time;
  int64_t loop_start_time;
  int64_t deal_start_time;
  int64_t deal_end_time;
  int64_t time_used;
	struct worker_thread_context *pThreadContext;

	pThreadContext = (struct worker_thread_context *)arg;

#if defined(HAVE_SYS_PRCTL_H) && defined(PR_SET_NAME)
  char name[32];
  sprintf(name, "[ET_CLUSTER %d]", (int)(pThreadContext -
        g_worker_thread_contexts) + 1);
  prctl(PR_SET_NAME, name, 0, 0, 0); 
#endif

	while (g_continue_flag) {
    //loop_start_time = CURRENT_NS();
    loop_start_time = get_current_time();
    deal_start_time = loop_start_time;
    schedule_sock_write(pThreadContext);

    GET_MAX_TIME_USED(max_write_loop_time_used);

    pThreadContext->stats.epoll_wait_count++;
		count = epoll_wait(pThreadContext->epoll_fd,
			pThreadContext->events, pThreadContext->alloc_size, 1);

    pThreadContext->stats.epoll_wait_time_used += get_current_time() - deal_start_time;
    GET_MAX_TIME_USED(max_epoll_time_used);

		if (count == 0) { //timeout
		}
    else if (count < 0) {
      if (errno != EINTR) {
        ink_fatal(1, "file: "__FILE__", line: %d, " \
            "call epoll_wait fail, " \
            "errno: %d, error info: %s\n",
            __LINE__, errno, strerror(errno));
      }
		}
    else {
      deal_epoll_events(pThreadContext, count);
      GET_MAX_TIME_USED(max_read_loop_time_used);
    }

    if (io_loop_interval > MIN_USLEEP_TIME) {
      remain_time = io_loop_interval - (int)((CURRENT_NS() -
          loop_start_time) / HRTIME_USECOND);
      if (remain_time >= MIN_USLEEP_TIME && remain_time <= io_loop_interval) {
        pThreadContext->stats.loop_usleep_count++;
        pThreadContext->stats.loop_usleep_time += remain_time;
        usleep(remain_time);
        GET_MAX_TIME_USED(max_usleep_time_used);
      }
    }
	}

	if ((result=pthread_mutex_lock(&worker_thread_lock)) != 0)
	{
		Error("file: "__FILE__", line: %d, " \
			"call pthread_mutex_lock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, strerror(result));
	}
	g_worker_thread_count--;
	if ((result=pthread_mutex_unlock(&worker_thread_lock)) != 0)
	{
		Error("file: "__FILE__", line: %d, " \
			"call pthread_mutex_unlock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, strerror(result));
	}

	return NULL;
}

int push_to_send_queue(SocketContext *pSockContext, OutMessage *pMessage,
    const MessagePriority priority)
{
	pthread_mutex_lock(&pSockContext->send_queues[priority].lock);
	if (pSockContext->send_queues[priority].head == NULL) {
		pSockContext->send_queues[priority].head = pMessage;
	}
	else {
		pSockContext->send_queues[priority].tail->next = pMessage;
	}
	pSockContext->send_queues[priority].tail = pMessage;
	pthread_mutex_unlock(&pSockContext->send_queues[priority].lock);

  __sync_fetch_and_add(&pSockContext->thread_context->stats.push_msg_count, 1);
  __sync_fetch_and_add(&pSockContext->thread_context->stats.push_msg_bytes,
      MSG_HEADER_LENGTH + pMessage->header.aligned_data_len);
  return 0;
}

int insert_into_send_queue_head(SocketContext *pSockContext, OutMessage *pMessage,
    const MessagePriority priority)
{
	pthread_mutex_lock(&pSockContext->send_queues[priority].lock);
	if (pSockContext->send_queues[priority].head == NULL) {
		pSockContext->send_queues[priority].head = pMessage;
    pSockContext->send_queues[priority].tail = pMessage;
	}
	else {
    if (pSockContext->send_queues[priority].head->bytes_sent == 0) { //head message not send yet
      pMessage->next = pSockContext->send_queues[priority].head;
      pSockContext->send_queues[priority].head = pMessage;
    }
    else {
      pMessage->next = pSockContext->send_queues[priority].head->next;
      pSockContext->send_queues[priority].head->next = pMessage;
      if (pMessage->next == NULL) {
        pSockContext->send_queues[priority].tail = pMessage;
      }
    }
	}
	pthread_mutex_unlock(&pSockContext->send_queues[priority].lock);

  __sync_fetch_and_add(&pSockContext->thread_context->stats.push_msg_count, 1);
  __sync_fetch_and_add(&pSockContext->thread_context->stats.push_msg_bytes,
      MSG_HEADER_LENGTH + pMessage->header.aligned_data_len);

  return 0;
}

