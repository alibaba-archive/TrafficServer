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
#include "nio.h"

int g_worker_thread_count = 0;

struct worker_thread_context *g_worker_thread_contexts = NULL;

static pthread_mutex_t worker_thread_lock;
//static char magic_buff[4];

static int set_epoll_recv_only(SocketContext *pSockContext);
static void *work_thread_entrance(void* arg);

message_deal_func g_msg_deal_func = NULL;
machine_change_notify_func g_machine_change_notify = NULL;

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

	bytes = g_socket_send_bufsize;
	if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
		(char *)&bytes, sizeof(int)) < 0)
	{
		Error("file: "__FILE__", line: %d, " \
			"setsockopt failed, errno: %d, error info: %s",
			__LINE__, errno, strerror(errno));
		return errno != 0 ? errno : ENOMEM;
	}

  bytes = g_socket_recv_bufsize;
	if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
		(char *)&bytes, sizeof(int)) < 0)
	{
		Error("file: "__FILE__", line: %d, " \
			"setsockopt failed, errno: %d, error info: %s",
			__LINE__, errno, strerror(errno));
		return errno != 0 ? errno : ENOMEM;
	}

  return 0;
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

  INIT_READER(pSockContext->reader, READ_BUFFER_SIZE);
  pSockContext->reader.recv_body_bytes = 0;

	pSockContext->epoll_events = EPOLLIN;

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

	return 0;
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

  Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
      "release %s:%d #%d message count: %d",
      __LINE__, pSockContext->machine->hostname,
      pSockContext->machine->cluster_port, pSockContext->sock, count);
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

  Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
      "before call machine_remove_connection", __LINE__);

  machine_remove_connection(pSockContext);
	close(pSockContext->sock);
  pSockContext->sock = -1;

  pSockContext->reader.blocks = NULL;
  pSockContext->reader.buffer = NULL;

  clear_send_queue(pSockContext);
  notify_connection_closed(pSockContext);

  if (pSockContext->connect_type == CONNECT_TYPE_CLIENT) {
    Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
      "before call make_connection", __LINE__);
    make_connection(pSockContext);
  }
  else {
    free_accept_sock_context(pSockContext);
  }
  
	return 0;
}

inline static int check_and_clear_send_event(SocketContext * pSockContext)
{
  int result;
  pthread_mutex_lock(&pSockContext->send_queues[0].lock);
  pthread_mutex_lock(&pSockContext->send_queues[1].lock);
  pthread_mutex_lock(&pSockContext->send_queues[2].lock);
  if (pSockContext->send_queues[0].head == NULL && 
      pSockContext->send_queues[1].head == NULL &&
      pSockContext->send_queues[2].head == NULL)
  {
    pthread_mutex_lock(&pSockContext->lock);
    set_epoll_recv_only(pSockContext);
    pthread_mutex_unlock(&pSockContext->lock);
    result = EAGAIN;
  }
  else {
    result = 0;
  }
  pthread_mutex_unlock(&pSockContext->send_queues[2].lock);
  pthread_mutex_unlock(&pSockContext->send_queues[1].lock);
  pthread_mutex_unlock(&pSockContext->send_queues[0].lock);

  return result;
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
  int remain_len;
  int priority;
  int start;
  int total_msg_count;
	int vec_count;
	int total_bytes;
	int write_bytes;
  int total_done_count;
	int result;
  int i, k;
  bool fetch_done;
  bool last_msg_complete;
  bool check_queue_empty;

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
    return check_and_clear_send_event(pSockContext);
	}

	write_bytes = writev(pSockContext->sock, write_vec, vec_count);
	if (write_bytes == 0) {   //connection closed
		Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, "
			"write fail, connection closed",
			__LINE__);
		return ECONNRESET;
	}
	else if (write_bytes < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return EAGAIN;
		}
    else if (errno == EINTR) {  //should try again
			Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__ ", line: %d, "
				"write fail, errno: %d, error info: %s", \
				__LINE__, errno, strerror(errno));
      return 0;
    }
		else {
			result = errno != 0 ? errno : EIO;
      Error("file: "__FILE__", line: %d, "
				"write fail, errno: %d, error info: %s", \
				__LINE__, result, strerror(result));
			return result;
		}
	}

	if (write_bytes == total_bytes && last_msg_complete) {  //all done
    for (i=0; i<PRIORITY_COUNT; i++) {
      msgs[i].pDoneMsgs = msgs[i].send_msgs;
      msgs[i].done_count = msgs[i].msg_count;
    }

    total_done_count = total_msg_count;
    pSockContext->queue_index = 0;
    result = 0;
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

    result = EAGAIN;
    if (total_done_count == 0) {
      return result;
    }
  }

  check_queue_empty = (total_done_count == total_msg_count);
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
    else {
      check_queue_empty = false;
    }
    pthread_mutex_unlock(&send_queue->lock);
  }

  if (check_queue_empty) {
    result = check_and_clear_send_event(pSockContext);
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

 /*
  Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
      "func_id: %d, data length: %d, recv_msg_count: %ld", __LINE__,
      pHeader->func_id, data_len, count + 1);
  */

  result = get_response_session(pHeader, &pMachineSessions,
      &pSessionEntry, pSockContext, &call_func, &user_data);
  if (result != 0) {
    /*
    if (pHeader->session_id.fields.ip != g_my_machine_ip) {  //request by other
      cluster_send_msg_internal(&pHeader->session_id, pSockContext,
          FUNC_ID_CONNECTION_CLOSED_NOTIFY, NULL, 0, PRIORITY_HIGH);
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
    g_msg_deal_func(pHeader->session_id, user_data,
        pHeader->func_id, blocks, pHeader->data_len);
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

  read_bytes = read(pSockContext->sock, pSockContext->reader.current,
      pSockContext->reader.buff_end - pSockContext->reader.current);
/*
  Debug(CLUSTER_DEBUG_TAG, "======file: " __FILE__ ", line: %d, "
      "%s:%d remain bytes: %ld, recv bytes: %d", __LINE__,
      pSockContext->machine->hostname,
      pSockContext->machine->cluster_port,
      pSockContext->reader.buff_end - pSockContext->reader.current,
      read_bytes);
*/
	if (read_bytes == 0) {
     Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
          "==type: %c, read fail, connection #%d closed", __LINE__,
          pSockContext->connect_type, pSockContext->sock);
      return ECONNRESET;
	}
	else if (read_bytes < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return EAGAIN;
		}
    else if (errno == EINTR) {  //should try again
			Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
				"read fail, errno: %d, error info: %s", \
				__LINE__, errno, strerror(errno));
      return 0;
    }
		else {
			result = errno != 0 ? errno : EIO;
			Error("file: " __FILE__ ", line: %d, "
				"read fail, errno: %d, error info: %s", \
				__LINE__, result, strerror(result));
			return result;
		}
	}

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

      if (current_true_body_bytes > 0) { //should alloc new buffer
        append_to_blocks(&pSockContext->reader, current_true_body_bytes);
        pSockContext->reader.recv_body_bytes = recv_body_bytes;
      }

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

    deal_message(pHeader, pSockContext, pSockContext->reader.blocks);

    pSockContext->reader.blocks = NULL;  //free memory pointer
    if (pSockContext->reader.recv_body_bytes > 0) {
      pSockContext->reader.recv_body_bytes = 0;
    }

    if (bFirstBlock) {
      pSockContext->reader.msg_header += MSG_HEADER_LENGTH + padding_body_bytes;
    }
    else {  //second block, no msg header
      pSockContext->reader.msg_header = pSockContext->reader.buffer->_data +
        padding_body_bytes;
    }
  }

	return result;
}

#define REMOVE_FROM_CHAIN(schedule_head, previous, pSockContext) \
  do { \
    if (previous == NULL) { \
      schedule_head = pSockContext->nio_next; \
    } \
    else { \
      previous->nio_next = pSockContext->nio_next; \
    } \
  } while (0)

static void nio_schedule(SocketContext *schedule_head)
{
  SocketContext *pSockContext;
  SocketContext *previous;
  int result;
  bool removed;

  while (schedule_head != NULL) {
    pSockContext = schedule_head;
    previous = NULL;
    while (pSockContext != NULL) {
      removed = false;
      if ((pSockContext->remain_events & EPOLLIN)) {
        if ((result=deal_read_event(pSockContext)) != 0) {
          pSockContext->remain_events &= ~EPOLLIN;
          if (pSockContext->remain_events == 0) {
            removed = true;
            REMOVE_FROM_CHAIN(schedule_head, previous, pSockContext);
          }
          else if (result !=  EAGAIN) {  //socket already closed
            pSockContext->remain_events = 0;
            removed = true;
            REMOVE_FROM_CHAIN(schedule_head, previous, pSockContext);
            close_socket(pSockContext);
          }
        }
      }

      if ((pSockContext->remain_events & EPOLLOUT)) {
        if ((result=deal_write_event(pSockContext)) != 0) {
          pSockContext->remain_events &= ~EPOLLOUT;
          if (pSockContext->remain_events == 0) {
            removed = true;
            REMOVE_FROM_CHAIN(schedule_head, previous, pSockContext);
          }
          else if (result !=  EAGAIN) {  //socket already closed
            pSockContext->remain_events = 0;
            removed = true;
            REMOVE_FROM_CHAIN(schedule_head, previous, pSockContext);
            close_socket(pSockContext);
          }
        }
      }

      if (!removed) {
        previous = pSockContext;
      }
      pSockContext = pSockContext->nio_next;
    }
  }
}

static void deal_epoll_events(struct worker_thread_context *
	pThreadContext, const int count)
{
	struct epoll_event *pEvent;
	struct epoll_event *pEventEnd;
  SocketContext *pSockContext;
  SocketContext *schedule_head;
  SocketContext *schedule_tail;

  schedule_head = NULL;
  schedule_tail = NULL;
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

    pSockContext->remain_events = (pEvent->events & EPOLLIN) |
      (pEvent->events & EPOLLOUT);

    if (schedule_head == NULL) {
      schedule_head = pSockContext;
    }
    else {
      schedule_tail->nio_next = pSockContext;
    }
    schedule_tail = pSockContext;
	}

  if (schedule_head != NULL) {
    schedule_tail->nio_next = NULL;
    nio_schedule(schedule_head);
  }

	return;
}

static void *work_thread_entrance(void* arg)
{
	int result;
	int count;
	struct worker_thread_context *pThreadContext;

	pThreadContext = (struct worker_thread_context *)arg;

//#if defined(HAVE_SYS_PRCTL_H) && defined(PR_SET_NAME)
#if defined(PR_SET_NAME)
  char name[32];
  sprintf(name, "[ET_CLUSTER %d]", (int)(pThreadContext -
        g_worker_thread_contexts) + 1);
  prctl(PR_SET_NAME, name, 0, 0, 0); 
#endif

	while (g_continue_flag) {
		count = epoll_wait(pThreadContext->epoll_fd,
			pThreadContext->events, pThreadContext->alloc_size, 1);
		if (count == 0) { //timeout
			continue;
		}
		if (count < 0) {
      if (errno != EINTR) {
        Error("file: "__FILE__", line: %d, " \
            "call epoll_wait fail, " \
            "errno: %d, error info: %s",
            __LINE__, errno, strerror(errno));
        sleep(1);
      }
			continue;
		}

		deal_epoll_events(pThreadContext, count);
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

inline static int notify_to_send(SocketContext *pSockContext)
{
	struct epoll_event event;
	event.data.ptr = pSockContext;
	event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
	if (epoll_ctl(pSockContext->thread_context->epoll_fd, EPOLL_CTL_MOD,
		pSockContext->sock, &event) != 0)
	{
		Error("file: " __FILE__ ", line: %d, "
			"epoll_ctl fail, errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
		return errno != 0 ? errno : ENOMEM;
	}

  pSockContext->epoll_events = EPOLLIN | EPOLLOUT;
	return 0;
}

static int set_epoll_recv_only(SocketContext *pSockContext)
{
	struct epoll_event event;
	event.data.ptr = pSockContext;
	event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
	if (epoll_ctl(pSockContext->thread_context->epoll_fd, EPOLL_CTL_MOD,
		pSockContext->sock, &event) != 0)
	{
		Error("file: " __FILE__ ", line: %d, "
			"epoll_ctl fail, errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
		return errno != 0 ? errno : ENOMEM;
	}
  pSockContext->epoll_events = EPOLLIN;

	return 0;
}

int push_to_send_queue(SocketContext *pSockContext, OutMessage *pMessage,
    const MessagePriority priority)
{
  bool notify;

	pthread_mutex_lock(&pSockContext->send_queues[priority].lock);
	if (pSockContext->send_queues[priority].head == NULL) {
		pSockContext->send_queues[priority].head = pMessage;
    notify = true;
	}
	else {
		pSockContext->send_queues[priority].tail->next = pMessage;
    notify = false;
	}
	pSockContext->send_queues[priority].tail = pMessage;
	pthread_mutex_unlock(&pSockContext->send_queues[priority].lock);

  if (notify) {
    pthread_mutex_lock(&pSockContext->lock);
    if ((pSockContext->epoll_events & EPOLLOUT) == 0) {
      notify_to_send(pSockContext);
    }
    pthread_mutex_unlock(&pSockContext->lock);
  }

  return 0;
}


