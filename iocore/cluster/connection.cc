//connection.c

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
#include <assert.h>
#include <sys/prctl.h>
#include <sys/epoll.h>
#include "Diags.h"
#include "global.h"
#include "sockopt.h"
#include "pthread_func.h"
#include "shared_func.h"
#include "nio.h"
#include "message.h"
#include "session.h"
#include "P_Cluster.h"
#include "ink_config.h"
#include "connection.h"

typedef enum {
  STATE_NOT_CONNECT = 0,
  STATE_CONNECTING,
  STATE_CONNECTED,
  STATE_SEND_DATA,
  STATE_RECV_DATA
} ConnectState;

typedef struct connect_context {
  SocketContext *pSockContext;
  int64_t connect_start_time; //connect start time in ms
  int64_t server_start_time;  //recv data start time in ms
  int reconnect_interval;     //reconnect interval in ms
  int connect_count; //already connect times
  int send_bytes;
  int recv_bytes;
  int total_bytes;
  ConnectState state;
  char buff[sizeof(MsgHeader) + sizeof(HelloMessage)];
  bool is_accept;    //true means server socket to accept
  bool need_reconnect;
  bool used;
  bool need_check_timeout;
} ConnectContext;

struct connection_thread_context
{
	int epoll_fd;
  int alloc_size;
	pthread_mutex_t lock;
	struct epoll_event *events;    //for epoll_wait

  ConnectContext *connections_buffer;  //memory pool for malloc
  ConnectContext **connections;  //existing connections
  int connection_count;   //current connection count
};

static struct connection_thread_context connect_thread_context;
static SocketContext *socket_contexts_pool = NULL;  //first element for accept

SocketContextsByMachine *g_machine_sockets = NULL;  //[dest ip % MAX_MACHINE_COUNT]

void *connect_worker_entrance(void *arg);

static int remove_connection(SocketContext *pSockContext, const bool needLock)
{
  ConnectContext **ppConnection;
  ConnectContext **ppConnectionEnd;
  ConnectContext **ppNext;

  /*
  Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
      "free connection, current count: %d", __LINE__,
      connect_thread_context.connection_count);
  */

  if (needLock) {
    pthread_mutex_lock(&connect_thread_context.lock);
  }

  ppConnectionEnd = connect_thread_context.connections +
    connect_thread_context.connection_count;
  for (ppConnection=connect_thread_context.connections; ppConnection<ppConnectionEnd;
      ppConnection++)
  {
    if ((*ppConnection)->pSockContext == pSockContext) {
      (*ppConnection)->used = false;
      (*ppConnection)->pSockContext = NULL;
      break;
    }
  }

  if (ppConnection == ppConnectionEnd) {
    Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
        "Can't found connection to release!", __LINE__);
    pthread_mutex_unlock(&connect_thread_context.lock);
    return ENOENT;
  }

  ppNext = ppConnection + 1;
  while (ppNext < ppConnectionEnd) {
    *(ppNext - 1) = *ppNext;
    ppNext++;
  }
  connect_thread_context.connection_count--;

  if (needLock) {
    pthread_mutex_unlock(&connect_thread_context.lock);
  }

  return 0;
}

static void close_connection(SocketContext *pSockContext)
{
  if (pSockContext->sock >= 0) {
    /*
    Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
        "close connection #%d %s:%d",
        __LINE__, pSockContext->sock,
        pSockContext->machine->hostname,
        pSockContext->machine->cluster_port);
    */

    close(pSockContext->sock);
    pSockContext->sock = -1;
  }
}

static void release_connection(SocketContext *pSockContext,
    const bool needLock)
{
  close_connection(pSockContext);
  if (pSockContext->connect_type == CONNECT_TYPE_SERVER) {
    remove_connection(pSockContext, needLock);
    free_accept_sock_context(pSockContext);
  }
}

inline static int get_machine_index(const unsigned int ip)
{
  int id;
  int count;
  int index;

  id = ip % MAX_MACHINE_COUNT;
  if (g_machine_sockets[id].ip == ip) {
    return id;
  }

  count = 1;
  while (count <= 16) {
    index = (id + count) % MAX_MACHINE_COUNT;
    if (g_machine_sockets[index].ip == ip) {
      return index;
    }
    count++;
  }

  return -1;
}

static int alloc_machine_index(const unsigned int ip)
{
  int id;
  int count;
  int index;

  id = ip % MAX_MACHINE_COUNT;
  if (g_machine_sockets[id].ip == 0) {
    return id;
  }

  count = 1;
  while (count <= 16) {
    index = (id + count) % MAX_MACHINE_COUNT;
    if (g_machine_sockets[index].ip == 0) {
      return index;
    }
    count++;
  }

  return -1;
}

static void fill_send_buffer(ConnectContext *pConnectContext,
    const int func_id)
{
  MsgHeader *pHeader;
  HelloMessage *pHello;

  pHeader = (MsgHeader *)pConnectContext->buff;
#ifdef CHECK_MAGIC_NUMBER
  pHeader->magic = MAGIC_NUMBER;
#endif

  pHeader->func_id = func_id;
  pHeader->data_len = sizeof(HelloMessage);
  pHeader->aligned_data_len = BYTE_ALIGN16(sizeof(HelloMessage));
  pHeader->session_id.fields.ip = g_my_machine_ip;
  pHeader->session_id.fields.timestamp = CURRENT_TIME();
  pHeader->session_id.fields.seq = 0;
  pHeader->msg_seq = 11111;   //do not create session

  pHello = (HelloMessage *)(pConnectContext->buff + sizeof(MsgHeader));
  pHello->major = CLUSTER_MAJOR_VERSION;
  pHello->minor = CLUSTER_MINOR_VERSION;
  pHello->min_major = MIN_CLUSTER_MAJOR_VERSION;
  pHello->min_minor = MIN_CLUSTER_MINOR_VERSION;

  pConnectContext->send_bytes = 0;
}

static int deal_hello_message(SocketContext *pSockContext, char *data)
{
  int proto_major = -1;
  int proto_minor = -1;
  uint32_t major;
  int expect_func_id;
  MsgHeader *pHeader;
  HelloMessage *pHelloMessage;

  pHeader = (MsgHeader *)data;
#ifdef CHECK_MAGIC_NUMBER
  if (pHeader->magic != MAGIC_NUMBER) {
    Error("file: "__FILE__", line: %d, " \
        "magic number: %08x != %08x", \
        __LINE__, pHeader->magic, MAGIC_NUMBER);
    return EINVAL;
  }
#endif

  if (pHeader->data_len != sizeof(HelloMessage)) {
    Error("file: "__FILE__", line: %d, " \
        "message length: %d != %d!", __LINE__,
        pHeader->data_len, (int)sizeof(HelloMessage));
    return EINVAL;
  }

  if (pSockContext->connect_type == CONNECT_TYPE_CLIENT) {
    expect_func_id = FUNC_ID_CLUSTER_HELLO_RESPONSE;
  }
  else {
    expect_func_id = FUNC_ID_CLUSTER_HELLO_REQUEST;
  }
  if (pHeader->func_id != expect_func_id) {
    Error("file: "__FILE__", line: %d, " \
        "invalid function id: %d != %d!", __LINE__,
        pHeader->func_id, expect_func_id);
    return EINVAL;
  }
  pHelloMessage = (HelloMessage *)(data + sizeof(MsgHeader));

  /**
   * Determine the message protocol major version to use, by stepping down
   * from current to the minimium level until a match is found.
   * Derive the minor number as follows, if the current (major, minor)
   * is the current node (major, minor) use the given minor number.
   * Otherwise, minor number is zero.
   **/
  for (major=pHelloMessage->major; major>=pHelloMessage->min_major; --major) {
    if ((major >= MIN_CLUSTER_MAJOR_VERSION) && (major <= CLUSTER_MAJOR_VERSION)) {
      proto_major = major;
    }
  }
  if (proto_major > 0) {
    /* Compute minor version */
    if (proto_major == (int)pHelloMessage->major) {
      proto_minor = pHelloMessage->minor;
      if (proto_minor != CLUSTER_MINOR_VERSION) {
        Warning("file: "__FILE__", line: %d, " \
            "Different clustering minor versions (%d,%d) for " \
            "node %u.%u.%u.%u, continuing", __LINE__,
            proto_minor, CLUSTER_MINOR_VERSION,
            DOT_SEPARATED(pSockContext->machine->ip));
      }
    } else {
      proto_minor = 0;
    }
  }
  else {
    Error("file: "__FILE__", line: %d, " \
        "Bad cluster major version range (%d-%d) for " \
        "node %u.%u.%u.%u, close connection", __LINE__,
        pHelloMessage->min_major, pHelloMessage->major,
        DOT_SEPARATED(pSockContext->machine->ip));
    return EINVAL;
  }

  /*
  Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
      "node: %u.%u.%u.%u, version: %d.%d", __LINE__,
      DOT_SEPARATED(pSockContext->machine->ip),
      proto_major, proto_minor);
  */

  pSockContext->machine->msg_proto_major = pHelloMessage->major;
  pSockContext->machine->msg_proto_minor = pHelloMessage->minor;
  return 0;
}


static int do_send_data(ConnectContext *pConnectContext)
{
  int bytes;
  int result;

  bytes = write(pConnectContext->pSockContext->sock, pConnectContext->buff +
      pConnectContext->send_bytes, pConnectContext->total_bytes -
      pConnectContext->send_bytes);
  if (bytes < 0) {
    result = errno != 0 ? errno : EAGAIN;
    if (result == EINTR) {
      Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
          "write failed, errno: %d, error info: %s", \
          __LINE__, result, strerror(result));
    }
    else if (!(result == EAGAIN)) {
      Error("file: "__FILE__", line: %d, " \
          "write failed, errno: %d, error info: %s", \
          __LINE__, result, strerror(result));
    }

    return result;
  }
  else if (bytes == 0) {
    Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
        "%s:%d connection closed", __LINE__,
        pConnectContext->pSockContext->machine->hostname,
        pConnectContext->pSockContext->machine->cluster_port);
    return ECONNRESET;
  }
  pConnectContext->send_bytes += bytes;

  return (pConnectContext->send_bytes == pConnectContext->total_bytes) ?
    0 : EAGAIN;
}

static int do_recv_data(ConnectContext *pConnectContext)
{
  int bytes;
  int result;

  bytes = read(pConnectContext->pSockContext->sock, pConnectContext->buff +
      pConnectContext->recv_bytes, pConnectContext->total_bytes -
      pConnectContext->recv_bytes);
  if (bytes < 0) {
    result = errno != 0 ? errno : EAGAIN;
    if (result == EINTR) {
      Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
          "read failed, errno: %d, error info: %s", \
          __LINE__, result, strerror(result));
    }
    else if (!(result == EAGAIN)) {
      Error("file: "__FILE__", line: %d, " \
          "read failed, errno: %d, error info: %s", \
          __LINE__, result, strerror(result));
    }

    return result;
  }
  else if (bytes == 0) {
    Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
        "%s:%d connection closed", __LINE__,
        pConnectContext->pSockContext->machine->hostname,
        pConnectContext->pSockContext->machine->cluster_port);
    return ECONNRESET;
  }
  pConnectContext->recv_bytes += bytes;

  return (pConnectContext->recv_bytes == pConnectContext->total_bytes) ?
    0 : EAGAIN;
}

static int check_socket_status(int sock)
{
	int result;
  socklen_t len;

  len = sizeof(result);
  if (getsockopt(sock, SOL_SOCKET, SO_ERROR,
        &result, &len) < 0)
  {
    result = errno != 0 ? errno : EACCES;
  }

  return result;
}

static int connection_handler(ConnectContext *pConnectContext)
{
  int result;
  SocketContext *pSockContext;
	struct epoll_event event;
  int op;

  pSockContext = pConnectContext->pSockContext;
  op = 0;
  event.events = 0;
  result = 0;
  switch (pConnectContext->state) {
    case STATE_CONNECTING:
      result = check_socket_status(pSockContext->sock);
      if (result != 0) {
        break;
      }
      pConnectContext->state = STATE_CONNECTED;
    case STATE_CONNECTED:
      if (pSockContext->connect_type == CONNECT_TYPE_CLIENT) {
        event.events = EPOLLOUT | EPOLLRDHUP | EPOLLET;
        op = EPOLL_CTL_MOD;
        pConnectContext->state = STATE_SEND_DATA;
        fill_send_buffer(pConnectContext, FUNC_ID_CLUSTER_HELLO_REQUEST);
      }
      else {  //server
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
        op = EPOLL_CTL_ADD;
        pConnectContext->state = STATE_RECV_DATA;
        pConnectContext->recv_bytes = 0;
        pConnectContext->server_start_time = CURRENT_MS(); 
      }

      break;
    case STATE_SEND_DATA:
      while ((result=do_send_data(pConnectContext)) == EINTR) {
      }

      if (result == EAGAIN) {
        event.events = EPOLLOUT | EPOLLRDHUP | EPOLLET;
        op = EPOLL_CTL_MOD;
        break;
      }
      else if (result != 0) {
        break;
      }

      //send data done
      if (pSockContext->connect_type == CONNECT_TYPE_CLIENT) {
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
        op = EPOLL_CTL_MOD;
        pConnectContext->state = STATE_RECV_DATA;
        pConnectContext->recv_bytes = 0;
        pConnectContext->server_start_time = CURRENT_MS(); 
      }
      else {  //server deal done
      }
      break;
    case STATE_RECV_DATA:
      while ((result=do_recv_data(pConnectContext)) == EINTR) {
      }

      if (result == EAGAIN) {
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
        op = EPOLL_CTL_MOD;
        break;
      }
      else if (result != 0) {
        break;
      }

      //recv data done
      result = deal_hello_message(pSockContext, pConnectContext->buff);
      if (pSockContext->connect_type == CONNECT_TYPE_CLIENT) {
      }
      else if (result == 0) {
        event.events = EPOLLOUT | EPOLLRDHUP | EPOLLET;
        op = EPOLL_CTL_MOD;
        pConnectContext->state = STATE_SEND_DATA;
        fill_send_buffer(pConnectContext, FUNC_ID_CLUSTER_HELLO_RESPONSE);
      }
      break;
    default:
      result = EINVAL;
      break;
  }

  if (event.events != 0) {
    event.data.ptr = pConnectContext;
    if (epoll_ctl(connect_thread_context.epoll_fd, op,
          pSockContext->sock, &event) == 0)
    {
      return 0;
    }

    result = errno != 0 ? errno : ENOMEM;
    Error("file: " __FILE__ ", line: %d, "
        "epoll_ctl fail, errno: %d, error info: %s", \
        __LINE__, result, strerror(result));
  }

  if (epoll_ctl(connect_thread_context.epoll_fd, EPOLL_CTL_DEL,
        pSockContext->sock, NULL) != 0)
  {
    result = errno != 0 ? errno : ENOMEM;
    Error("file: " __FILE__ ", line: %d, "
        "epoll_ctl #%d fail, errno: %d, error info: %s", \
        __LINE__, pSockContext->sock,
        result, strerror(result));
  }

  remove_connection(pSockContext, true);
  if (result == 0) {
    result = machine_add_connection(pSockContext);
    if (result == 0) {
      machine_up_notify(pSockContext->machine);
    }
  }

  if (result != 0) {
    close_connection(pSockContext);
    if (pSockContext->connect_type == CONNECT_TYPE_SERVER) {
      free_accept_sock_context(pSockContext);
    }
  }

  return result;
}

#ifdef USE_MULTI_ALLOCATOR
static void check_init_allocator(SocketContext *pSockContext)
{
  char name[64];
  int index;

  if (pSockContext->out_msg_allocator == NULL) {
    index = pSockContext - socket_contexts_pool;
    sprintf(name, "OutMessage_%d", index);
    pSockContext->out_msg_allocator = new Allocator(name,
        sizeof(OutMessage), 512);

    sprintf(name, "InMessage_%d", index);
    pSockContext->in_msg_allocator = new Allocator(name,
        sizeof(InMessage), 128);
  }
}
#endif

static SocketContext *alloc_connect_sock_context(const unsigned int machine_ip)
{
  SocketContext *pSockContext;
  int machine_id;

	pthread_mutex_lock(&connect_thread_context.lock);
  if ((machine_id=get_machine_index(machine_ip)) < 0) {
    if ((machine_id=alloc_machine_index(machine_ip)) < 0) {
      pthread_mutex_unlock(&connect_thread_context.lock);
      return NULL;
    }

    g_machine_sockets[machine_id].ip = machine_ip;
  }

  pSockContext = g_machine_sockets[machine_id].connect_free_list;
  if (pSockContext != NULL) {
    g_machine_sockets[machine_id].connect_free_list =
      pSockContext->next;

#ifdef USE_MULTI_ALLOCATOR
    check_init_allocator(pSockContext);
#endif
  }
	pthread_mutex_unlock(&connect_thread_context.lock);

  return pSockContext;
}

SocketContext *alloc_accept_sock_context(const unsigned int machine_ip)
{
  SocketContext *pSockContext;
  int machine_id;

	pthread_mutex_lock(&connect_thread_context.lock);
  if ((machine_id=get_machine_index(machine_ip)) < 0) {
    if ((machine_id=alloc_machine_index(machine_ip)) < 0) {
      pthread_mutex_unlock(&connect_thread_context.lock);
      return NULL;
    }

    g_machine_sockets[machine_id].ip = machine_ip;
  }

  pSockContext = g_machine_sockets[machine_id].accept_free_list;
  if (pSockContext != NULL) {
    g_machine_sockets[machine_id].accept_free_list =
      pSockContext->next;

#ifdef USE_MULTI_ALLOCATOR
    check_init_allocator(pSockContext);
#endif
  }
	pthread_mutex_unlock(&connect_thread_context.lock);

  return pSockContext;
}

static void free_connect_sock_context(SocketContext *pSockContext,
    const bool needLock)
{
  int machine_id;
  if ((machine_id=get_machine_index(pSockContext->machine->ip)) < 0) {
    return;
  }

  if (needLock) {
    pthread_mutex_lock(&connect_thread_context.lock);
  }

  pSockContext->next = g_machine_sockets[machine_id].connect_free_list;
  g_machine_sockets[machine_id].connect_free_list = pSockContext;

  if (needLock) {
    pthread_mutex_unlock(&connect_thread_context.lock);
  }
}

static int alloc_socket_contexts(const int connections_per_machine,
    SocketContext **pool)
{
  int result;
  int bytes;
  int i;
  int total_connections;

  SocketContext *pSockContext;
  SocketContext *pSockContextEnd;

  total_connections = connections_per_machine * MAX_MACHINE_COUNT + 1;
  bytes = sizeof(SocketContext) * total_connections;
  *pool =	(SocketContext *)malloc(bytes);
  if (*pool == NULL) {
    Error("file: "__FILE__", line: %d, " \
        "malloc %d bytes fail, errno: %d, error info: %s", \
        __LINE__, bytes, errno, strerror(errno));
    return errno != 0 ? errno : ENOMEM;
  }
  memset(*pool, 0, bytes);

  pSockContextEnd = *pool + total_connections;
  for (pSockContext=*pool; pSockContext<pSockContextEnd;
      pSockContext++)
  {
    for (i=0; i<PRIORITY_COUNT; i++) {
      if ((result=init_pthread_lock(&pSockContext->send_queues[i].lock)) != 0) {
        return result;
      }
    }
  }

  return 0;
}

static int init_socket_contexts()
{
  int result;
  int half_connections_per_machine;
  int machine_index;
  int thread_index;
  int k;
  SocketContext *pSockContext;

  if ((result=alloc_socket_contexts(g_connections_per_machine,
          &socket_contexts_pool)) != 0)
  {
    return result;
  }

  half_connections_per_machine = g_connections_per_machine / 2;
  pSockContext = socket_contexts_pool + 1;   //0 for server accept
  thread_index = 0;
  for (machine_index=0; machine_index<MAX_MACHINE_COUNT; machine_index++) {
    for (k=0; k<half_connections_per_machine; k++) {
      pSockContext->connect_type = CONNECT_TYPE_SERVER;
      pSockContext->next = g_machine_sockets[machine_index].accept_free_list;
      g_machine_sockets[machine_index].accept_free_list = pSockContext;
      pSockContext->thread_context = g_worker_thread_contexts +
        thread_index++ % g_work_threads;
      pSockContext++;
    }

    for (k=0; k<half_connections_per_machine; k++) {
      pSockContext->connect_type = CONNECT_TYPE_CLIENT;
      pSockContext->next = g_machine_sockets[machine_index].connect_free_list;
      g_machine_sockets[machine_index].connect_free_list = pSockContext;
      pSockContext->thread_context = g_worker_thread_contexts +
        thread_index++ % g_work_threads;
      pSockContext++;
    }
  }

  return 0;
}

int connection_init()
{
  int result;
  int bytes;

	bytes = sizeof(SocketContextsByMachine) * MAX_MACHINE_COUNT;
	g_machine_sockets = (SocketContextsByMachine *)malloc(bytes);
	if (g_machine_sockets == NULL) {
		Error("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail, errno: %d, error info: %s", \
			__LINE__, bytes, errno, strerror(errno));
		return errno != 0 ? errno : ENOMEM;
	}
	memset(g_machine_sockets, 0, bytes);

  connect_thread_context.alloc_size = MAX_MACHINE_COUNT *
    g_connections_per_machine  + 1;
  bytes = sizeof(struct epoll_event) * connect_thread_context.alloc_size;
  connect_thread_context.events = (struct epoll_event *)malloc(bytes);
  if (connect_thread_context.events == NULL) {
    Error("file: "__FILE__", line: %d, " \
        "malloc %d bytes fail, errno: %d, error info: %s", \
        __LINE__, bytes, errno, strerror(errno));
    return errno != 0 ? errno : ENOMEM;
  }

	bytes = sizeof(ConnectContext) * connect_thread_context.alloc_size;
	connect_thread_context.connections_buffer = (ConnectContext *)malloc(bytes);
	if (connect_thread_context.connections_buffer == NULL) {
		Error("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail, errno: %d, error info: %s", \
			__LINE__, bytes, errno, strerror(errno));
		return errno != 0 ? errno : ENOMEM;
	}
	memset(connect_thread_context.connections_buffer, 0, bytes);

	bytes = sizeof(ConnectContext *) * connect_thread_context.alloc_size;
	connect_thread_context.connections = (ConnectContext **)malloc(bytes);
	if (connect_thread_context.connections == NULL) {
		Error("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail, errno: %d, error info: %s", \
			__LINE__, bytes, errno, strerror(errno));
		return errno != 0 ? errno : ENOMEM;
	}
	memset(connect_thread_context.connections, 0, bytes);
  connect_thread_context.connection_count = 0;

  connect_thread_context.epoll_fd = epoll_create(
      connect_thread_context.alloc_size);
  if (connect_thread_context.epoll_fd < 0) {
    Error("file: " __FILE__ ", line: %d, "
        "poll_create fail, errno: %d, error info: %s", \
        __LINE__, errno, strerror(errno));
    return errno != 0 ? errno : ENOMEM;
  }

  if ((result=init_pthread_lock(&connect_thread_context.lock)) != 0) {
    return result;
  }

  if ((result=init_socket_contexts()) != 0) {
    return result;

  }

  return 0;
}

void connection_destroy()
{
}

static ConnectContext *find_connection(SocketContext *pSockContext)
{
  ConnectContext **ppConnection;
  ConnectContext **ppConnectionEnd;

	pthread_mutex_lock(&connect_thread_context.lock);
  ppConnectionEnd = connect_thread_context.connections +
    connect_thread_context.connection_count;
  for (ppConnection=connect_thread_context.connections; ppConnection<ppConnectionEnd;
      ppConnection++)
  {
    if ((*ppConnection)->pSockContext == pSockContext) {
      break;
    }
  }
  pthread_mutex_unlock(&connect_thread_context.lock);

  return (ppConnection == ppConnectionEnd) ?  NULL: *ppConnection;
}

static int do_connect(ConnectContext *pConnectContext, const bool needLock)
{
  int result;
	struct epoll_event event;
  struct sockaddr_in addr;
  SocketContext *pSockContext;

  pSockContext = pConnectContext->pSockContext;
  pSockContext->sock = socket(AF_INET, SOCK_STREAM, 0);
  pConnectContext->connect_count++;
  pConnectContext->state = STATE_CONNECTING;
  if (pSockContext->sock < 0) {
    Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
        "socket create failed, errno: %d, error info: %s", \
        __LINE__, errno, strerror(errno));
    return errno != 0 ? errno : EMFILE;
  }

  if ((result=set_nonblock(pSockContext->sock)) != 0) {
    close_connection(pSockContext);
    return result;
  }
  tcpsetnodelay(pSockContext->sock);

  addr.sin_family = PF_INET;
  addr.sin_port = htons(pSockContext->machine->cluster_port);
  result = inet_aton(pSockContext->machine->hostname, &addr.sin_addr);
  if (result == 0) {
    close_connection(pSockContext);
    remove_connection(pSockContext, needLock);
    return EINVAL;
  }

  pConnectContext->connect_start_time = CURRENT_MS();   //connect start time
  if (connect(pSockContext->sock, (const struct sockaddr*)&addr, \
        sizeof(addr)) == 0)  //success
  {
    pConnectContext->state = STATE_CONNECTED;
    pConnectContext->need_check_timeout = true;
    return connection_handler(pConnectContext);
  }

  result = errno != 0 ? errno : EINPROGRESS;
  if (result != EINPROGRESS) {
    Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
        "connect to %s:%d failed, errno: %d, error info: %s", \
        __LINE__, pSockContext->machine->hostname,
        pSockContext->machine->cluster_port, result, strerror(result));
    close_connection(pSockContext);
    return result;
  }

	event.data.ptr = pConnectContext;
	event.events = EPOLLOUT | EPOLLRDHUP | EPOLLET;
	if (epoll_ctl(connect_thread_context.epoll_fd, EPOLL_CTL_ADD,
		pSockContext->sock, &event) != 0)
	{
    result = errno != 0 ? errno : ENOMEM;
		Error("file: " __FILE__ ", line: %d, "
			"epoll_ctl fail, errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
    close_connection(pSockContext);
		return result;
	}

  pConnectContext->need_check_timeout = true;
  return result;
}

static ConnectContext *alloc_connect_context()
{
  ConnectContext *pConnectContext;
  ConnectContext *pConnectEnd;

	pthread_mutex_lock(&connect_thread_context.lock);
  if (connect_thread_context.connection_count >=
      connect_thread_context.alloc_size)
  {
    pthread_mutex_unlock(&connect_thread_context.lock);
    Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
        "exceeds max connection: %d",
        __LINE__, connect_thread_context.alloc_size);
    return NULL;
  }

  pConnectEnd = connect_thread_context.connections_buffer +
    connect_thread_context.alloc_size;
  for (pConnectContext=connect_thread_context.connections_buffer;
      pConnectContext<pConnectEnd; pConnectContext++)
  {
    if (!pConnectContext->used) {
      break;
    }
  }
  if (pConnectContext == pConnectEnd) {
    pthread_mutex_unlock(&connect_thread_context.lock);
    Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
        "alloc connection from buffer fail", __LINE__);
    return NULL;
  }

  pConnectContext->used = true;
  connect_thread_context.connections[connect_thread_context.
    connection_count++] = pConnectContext;
	pthread_mutex_unlock(&connect_thread_context.lock);

  pConnectContext->need_reconnect = false;
  pConnectContext->need_check_timeout = false;
  pConnectContext->reconnect_interval = 100;
  pConnectContext->connect_count = 0;
  pConnectContext->state = STATE_NOT_CONNECT;
  pConnectContext->send_bytes = 0;
  pConnectContext->recv_bytes = 0;
  pConnectContext->total_bytes = sizeof(MsgHeader) + sizeof(HelloMessage);

  return pConnectContext;
}

int machine_stop_reconnect(ClusterMachine *m)
{
  int count;
  ConnectContext **ppConnection;
  ConnectContext **ppConnectionEnd;

  count = 0;
	pthread_mutex_lock(&connect_thread_context.lock);
  ppConnectionEnd = connect_thread_context.connections +
    connect_thread_context.connection_count;
  for (ppConnection=connect_thread_context.connections; ppConnection<ppConnectionEnd;
      ppConnection++)
  {
    if ((*ppConnection)->pSockContext->machine == m) {
      count++;
      (*ppConnection)->need_reconnect = false;
    }
  }
	pthread_mutex_unlock(&connect_thread_context.lock);

  return count > 0 ? 0 : ENOENT;
}

int machine_make_connections(ClusterMachine *m)
{
  int half_connections_per_machine;
  int i;
  int result;
  SocketContext *pSockContext;

  if ((result=init_machine_sessions(m, false)) != 0) {
    return result;
  }

  half_connections_per_machine = g_connections_per_machine / 2;
  for (i=0; i<half_connections_per_machine; i++) {
    pSockContext = alloc_connect_sock_context(m->ip);
    if (pSockContext == NULL) {
      return ENOSPC;
    }

    pSockContext->machine = m;
    make_connection(pSockContext);
  }

  return 0;
}

int make_connection(SocketContext *pSockContext)
{
  ConnectContext *pConnectContext;

  /*
  Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
      "alloc connection, current count: %d", __LINE__,
      connect_thread_context.connection_count);
  */

  if (find_connection(pSockContext) != NULL) {
    Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
        "connection: %p already exist!", __LINE__, pSockContext);
    return EEXIST;
  }

  pConnectContext = alloc_connect_context();
  if (pConnectContext == NULL) {
    return ENOSPC;
  }

  pConnectContext->need_reconnect = true;
  pConnectContext->reconnect_interval = 100;
  pConnectContext->pSockContext = pSockContext;
  return do_connect(pConnectContext, true);
}

int connection_manager_init(const unsigned int my_ip, const int port)
{
  ConnectContext *pConnectContext;
	char bind_addr[IP_ADDRESS_SIZE];
	struct epoll_event event;
  int result;
	int server_sock;

  assert(MSG_HEADER_LENGTH % 16 == 0);
	*bind_addr = '\0';
	server_sock = socketServer(bind_addr, g_server_port, &result);
	if (server_sock < 0)
	{
		return errno != 0 ? errno : EIO;
	}

	if ((result=tcpsetserveropt(server_sock, 0)) != 0)
	{
		return result;
	}

  if ((result=set_nonblock(server_sock)) != 0) {
    return result;
  }

  if ((result=init_machines()) != 0) { 
		return result;
  }

  if (my_ip > 0) {
    g_my_machine_ip = my_ip;
    add_machine(my_ip, port);
  }

	if ((result=nio_init()) != 0 || (result=connection_init()) != 0
      || (result=session_init()) != 0)
	{
		return result;
	}

  pConnectContext = alloc_connect_context();
  if (pConnectContext == NULL) {
    return ENOSPC;
  }

  pConnectContext->pSockContext = socket_contexts_pool + 0;
  pConnectContext->is_accept = true;
  pConnectContext->pSockContext->sock = server_sock;

	event.data.ptr = pConnectContext;
	event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
	if (epoll_ctl(connect_thread_context.epoll_fd, EPOLL_CTL_ADD,
		server_sock, &event) != 0)
	{
    result = errno != 0 ? errno : ENOMEM;
		Error("file: " __FILE__ ", line: %d, "
			"epoll_ctl fail, errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
		return result;
	}

  return 0;
}

void connection_manager_destroy()
{
}

int connection_manager_start(pthread_t *tid)
{
  int result;
  if ((result=pthread_create(tid, NULL,
          connect_worker_entrance, NULL)) != 0)
  {
    Error("file: "__FILE__", line: %d, " \
        "create thread failed, " \
        "errno: %d, error info: %s",
        __LINE__, result, strerror(result));
    return result;
  }

  return 0;
}

static int close_timeout_connections()
{
#define MAX_TIMEOUT_SOCKET_COUNT  64
  ConnectContext **ppConnection;
  ConnectContext **ppConnectionEnd;
  SocketContext *pSockContext;
  ConnectContext *timeoutConnectContexts[MAX_TIMEOUT_SOCKET_COUNT];
  int timeout_count;
  int i;
  bool bTimeout;

  timeout_count = 0;
	pthread_mutex_lock(&connect_thread_context.lock);
  ppConnectionEnd = connect_thread_context.connections +
    connect_thread_context.connection_count;
  ppConnection = connect_thread_context.connections;
  while (ppConnection < ppConnectionEnd) {
    pSockContext = (*ppConnection)->pSockContext;
    if (!(*ppConnection)->need_check_timeout || pSockContext->sock < 0) {
      ppConnection++;
      continue;
    }

    if ((*ppConnection)->state == STATE_RECV_DATA) {
      bTimeout = (CURRENT_MS() - (*ppConnection)->server_start_time >= 1000);
    }
    else {
      bTimeout = ((*ppConnection)->state == STATE_CONNECTING &&
          CURRENT_MS() - (*ppConnection)->connect_start_time >=
          g_connect_timeout * 1000);
    }

    if (bTimeout) {
      timeoutConnectContexts[timeout_count++] = *ppConnection;
      if (timeout_count == MAX_TIMEOUT_SOCKET_COUNT) {
        break;
      }
    }

    ppConnection++;
  }

  for (i=0; i<timeout_count; i++) {
    pSockContext = timeoutConnectContexts[i]->pSockContext;
    if (epoll_ctl(connect_thread_context.epoll_fd, EPOLL_CTL_DEL,
          pSockContext->sock, NULL) != 0)
    {
      Error("file: " __FILE__ ", line: %d, "
          "epoll_ctl #%d fail, errno: %d, error info: %s", \
          __LINE__, pSockContext->sock,
          errno, strerror(errno));
    }

    Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
        "close timeout %s connection #%d %s:%d, type: %c",
        __LINE__, timeoutConnectContexts[i]->state == STATE_RECV_DATA ?
        "recv" : "connect", pSockContext->sock,
        pSockContext->machine->hostname,
        pSockContext->machine->cluster_port,
        pSockContext->connect_type);

    release_connection(pSockContext, false);
  }

	pthread_mutex_unlock(&connect_thread_context.lock);
  return 0;
}

static int do_reconnect()
{
  ConnectContext **ppConnection;
  ConnectContext **ppConnectionEnd;
	SocketContext *pSockContext;
  int max_reconnect_interval;

	pthread_mutex_lock(&connect_thread_context.lock);
  ppConnectionEnd = connect_thread_context.connections +
    connect_thread_context.connection_count;
  ppConnection=connect_thread_context.connections;
  while (ppConnection<ppConnectionEnd) {
    if ((*ppConnection)->pSockContext->sock >= 0) {  //already in progress or connected
      ppConnection++;
      continue;
    }

    if ((*ppConnection)->need_reconnect) {
      if ((*ppConnection)->connect_count > 0) {  //should reconnect
        if (CURRENT_MS() - (*ppConnection)->connect_start_time <
            (*ppConnection)->reconnect_interval)
        {
          ppConnection++;
          continue;
        }

        (*ppConnection)->reconnect_interval *= 2;
        if ((*ppConnection)->pSockContext->machine->dead) {
          max_reconnect_interval = 1000;
        }
        else {
          max_reconnect_interval = 30000;
        }
        if ((*ppConnection)->reconnect_interval > max_reconnect_interval) {
          (*ppConnection)->reconnect_interval = max_reconnect_interval;
        }
        (*ppConnection)->need_check_timeout = false;
        do_connect(*ppConnection, false);
        ppConnection++;
        ppConnectionEnd = connect_thread_context.connections +
          connect_thread_context.connection_count;
      }
    }
    else {   //should release
      pSockContext = (*ppConnection)->pSockContext;
      if (remove_connection(pSockContext, false) == 0) {  //removed
        ppConnectionEnd = connect_thread_context.connections +
          connect_thread_context.connection_count;
      }
      else {
        ppConnection++;
      }

      free_connect_sock_context(pSockContext, false);
    }
  }
	pthread_mutex_unlock(&connect_thread_context.lock);

  return 0;
}

static int deal_income_connection(const int incomesock)
{
	int result;
  char client_ip[IP_ADDRESS_SIZE];
  in_addr_t ip;
  ConnectContext *pConnectContext;
	SocketContext *pSockContext;
  ClusterMachine *machine;

  if ((result=set_nonblock(incomesock)) != 0) {
    return result;
  }
  tcpsetnodelay(incomesock);

  ip = getPeerIpaddr(incomesock, client_ip, sizeof(client_ip));
  machine = get_machine(ip, g_server_port);
  if (machine == NULL) {
		Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
			"client: %s not in my machine list", \
			__LINE__, client_ip);
    return ENOENT;
  }

  /*
  Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
      "client_ip: %s, sock: #%d", __LINE__,
      client_ip, incomesock);
  */

  pSockContext = alloc_accept_sock_context(machine->ip);
	if (pSockContext == NULL) {
		Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
			"client: %s, too many income connections, exceeds %d",
      __LINE__, client_ip, g_connections_per_machine / 2);
		return ENOSPC;
	}

	pSockContext->sock = incomesock;
	pSockContext->machine = machine;

  pConnectContext = alloc_connect_context();
  if (pConnectContext == NULL) {
    free_accept_sock_context(pSockContext);
    return ENOSPC;
  }

  pConnectContext->pSockContext = pSockContext;
  pConnectContext->state = STATE_CONNECTED;
  pConnectContext->need_check_timeout = true;
  connection_handler(pConnectContext);
  return 0;
}

static int deal_accept_event(SocketContext *pSockContext)
{
  int incomesock;
  int result;
  struct sockaddr_in inaddr;
  socklen_t sockaddr_len;

  sockaddr_len = sizeof(inaddr);
  incomesock = accept(pSockContext->sock, (struct sockaddr*)&inaddr,
      &sockaddr_len);
  if (incomesock < 0) {  //error
    result = errno != 0 ? errno : EAGAIN;
    if (result == EINTR) {
      Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
          "accept failed, " \
          "errno: %d, error info: %s", \
          __LINE__, errno, strerror(errno));
      return 0; //should try again
    }
    else if (!(errno == EAGAIN)) {
      Error("file: "__FILE__", line: %d, " \
          "accept failed, " \
          "errno: %d, error info: %s", \
          __LINE__, result, strerror(result));
    }

    return result;
  }

  result = deal_income_connection(incomesock);
  if (result != 0)
  {
    close(incomesock);
  }

  return 0;
}

static int deal_connect_events(const int count)
{
	struct epoll_event *pEvent;
	struct epoll_event *pEventEnd;
  ConnectContext *pConnectContext;
  SocketContext *pSockContext;
  //static int counter = 0;

	pEventEnd = connect_thread_context.events + count;
	for (pEvent=connect_thread_context.events; pEvent<pEventEnd; pEvent++) {
    pConnectContext = (ConnectContext *)pEvent->data.ptr;
	  pSockContext = pConnectContext->pSockContext;

    if (pConnectContext->is_accept) {
      while (deal_accept_event(pSockContext) == 0) {
      }
      continue;
    }

    /*
    Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
        "%d. connections #%d  %s:%d, type: %c, epoll events: %d", __LINE__,
        ++counter, pSockContext->sock, pSockContext->machine->hostname,
        pSockContext->machine->cluster_port, pSockContext->connect_type,
        pEvent->events);
    */

    if ((pEvent->events & EPOLLRDHUP) || (pEvent->events & EPOLLERR) ||
        (pEvent->events & EPOLLHUP))
    {
        Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
            "connect to %s:%d fail, connection closed",
            __LINE__, pSockContext->machine->hostname,
            pSockContext->machine->cluster_port);
        release_connection(pSockContext, true);
        continue;
    }

    /*
    Debug(CLUSTER_DEBUG_TAG, "file: " __FILE__ ", line: %d, "
        "====in: %d, out: %d====", __LINE__, (pEvent->events & EPOLLIN),
        (pEvent->events & EPOLLOUT));
        */

    if ((pEvent->events & EPOLLIN) || (pEvent->events & EPOLLOUT)) {
      connection_handler(pConnectContext);
    }
	}

	return 0;
}

void *connect_worker_entrance(void *arg)
{
  int count;
  time_t last_cluster_stat_time;
#if defined(TRIGGER_STAT_FLAG) || defined(MSG_TIME_STAT_FLAG)
  time_t last_msg_stat_time;
#endif

#if defined(HAVE_SYS_PRCTL_H) && defined(PR_SET_NAME)
  prctl(PR_SET_NAME, "[ET_CLUSTER 0]", 0, 0, 0); 
#endif

  last_cluster_stat_time = CURRENT_TIME();
#if defined(TRIGGER_STAT_FLAG) || defined(MSG_TIME_STAT_FLAG)
  last_msg_stat_time = CURRENT_TIME();
#endif

  while (g_continue_flag) {
#ifdef USE_CLUSTER_TIME
    timeval tv;
    gettimeofday(&tv, NULL);
    cluster_current_time = tv.tv_sec * HRTIME_SECOND +
      tv.tv_usec * HRTIME_USECOND;
#endif

    if (CURRENT_TIME() - last_cluster_stat_time > 1) {
      log_session_stat();
      log_nio_stats();
      last_cluster_stat_time = CURRENT_TIME();
    }

#if defined(TRIGGER_STAT_FLAG) || defined(MSG_TIME_STAT_FLAG)
      if (CURRENT_TIME() - last_msg_stat_time >= 60) {
#ifdef TRIGGER_STAT_FLAG
        log_trigger_stat();
#endif

#ifdef MSG_TIME_STAT_FLAG
        log_msg_time_stat();
#endif

        last_msg_stat_time = CURRENT_TIME();
      }
#endif

    if (connect_thread_context.connection_count > 1) {
      do_reconnect();
    }

		count = epoll_wait(connect_thread_context.epoll_fd,
			connect_thread_context.events, connect_thread_context.alloc_size, 1000);
		if (count == 0) { //timeout
      if (connect_thread_context.connection_count > 1) {
        close_timeout_connections();
      }
			continue;
		}
		if (count < 0) {
      if (errno != EINTR) {
        ink_fatal(1, "file: "__FILE__", line: %d, " \
            "call epoll_wait fail, " \
            "errno: %d, error info: %s\n",
            __LINE__, errno, strerror(errno));
      }
			continue;
		}

		deal_connect_events(count);
  }

  return NULL;
}

void free_accept_sock_context(SocketContext *pSockContext)
{
  int machine_id;
  if ((machine_id=get_machine_index(pSockContext->machine->ip)) < 0) {
    return;
  }

	pthread_mutex_lock(&connect_thread_context.lock);
  pSockContext->next = g_machine_sockets[machine_id].accept_free_list;
  g_machine_sockets[machine_id].accept_free_list = pSockContext;
	pthread_mutex_unlock(&connect_thread_context.lock);
}

int add_machine_sock_context(SocketContext *pSockContext)
{
  SocketContextArray *contextArray;
  SocketContext **oldContexts;
  SocketContext **newContexts;
  int bytes;
  int machine_id;
  if ((machine_id=get_machine_index(pSockContext->machine->ip)) < 0) {
    return ENOENT;
  }

	pthread_mutex_lock(&connect_thread_context.lock);
  contextArray = &g_machine_sockets[machine_id].connected_list;
  if (contextArray->count >= contextArray->alloc_size) {
    if (contextArray->alloc_size == 0) {
      contextArray->alloc_size = 64;
    }
    else {
      contextArray->alloc_size *= 2;
    }

    bytes = sizeof(SocketContext *) * contextArray->alloc_size;
    newContexts = (SocketContext **)malloc(bytes);
    if (newContexts == NULL) {
      Error("file: "__FILE__", line: %d, " \
          "malloc %d bytes fail, errno: %d, error info: %s", \
          __LINE__, bytes, errno, strerror(errno));
      pthread_mutex_unlock(&connect_thread_context.lock);
      return errno != 0 ? errno : ENOMEM;
    }

    memset(newContexts, 0, bytes);
    if (contextArray->count > 0) {
      memcpy(newContexts, contextArray->contexts,
          sizeof(SocketContext *) * contextArray->count);
    }

    oldContexts = contextArray->contexts;
    contextArray->contexts = newContexts;
    if (oldContexts != NULL) {
      free(oldContexts);
    }
  }

  contextArray->contexts[contextArray->count++] = pSockContext;
	pthread_mutex_unlock(&connect_thread_context.lock);

  return 0;
}

int remove_machine_sock_context(SocketContext *pSockContext)
{
  SocketContextArray *contextArray;
  unsigned int found;
  unsigned int i;
  int machine_id;

  if ((machine_id=get_machine_index(pSockContext->machine->ip)) < 0) {
    return ENOENT;
  }

  pthread_mutex_lock(&connect_thread_context.lock);
  contextArray = &g_machine_sockets[machine_id].connected_list;
  if (contextArray->count == 0) {
    pthread_mutex_unlock(&connect_thread_context.lock);
    return ENOENT;
  }

  for (found=0; found<contextArray->count; found++) {
    if (contextArray->contexts[found] == pSockContext) {
      break;
    }
  }

  if (found == contextArray->count) {
    pthread_mutex_unlock(&connect_thread_context.lock);
    return ENOENT;
  }

  for (i=found+1; i<contextArray->count; i++) {
    contextArray->contexts[i-1] = contextArray->contexts[i];
  }
  contextArray->contexts[--contextArray->count] = NULL;
  pthread_mutex_unlock(&connect_thread_context.lock);

  return 0;
}

SocketContext *get_socket_context(const ClusterMachine *machine)
{
	SocketContextArray *pSocketContextArray;
  int machine_id;
  int context_count;
	unsigned int context_index;

  if ((machine_id=get_machine_index(machine->ip)) < 0) {
    Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
        "the index of ip addr: %s not exist", __LINE__, machine->hostname);
    return NULL;
  }

	pSocketContextArray = &g_machine_sockets[machine_id].connected_list;
	context_count = pSocketContextArray->count;
	if (context_count <= 0) {
    /*
    Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
        "the socket context count of ip addr: %s is zero",
        __LINE__, machine->hostname);
    */
		return NULL;
	}

	context_index = __sync_fetch_and_add(&pSocketContextArray->index, 1) %
    context_count;
	if (context_index >= pSocketContextArray->count) {
		return NULL;
	}

  return pSocketContextArray->contexts[context_index];
}

