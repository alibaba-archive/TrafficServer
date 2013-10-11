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

#ifndef _CLUSTER_TYPES_H_
#define _CLUSTER_TYPES_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clusterinterface.h"
#include "libts.h"

#define IP_ADDRESS_SIZE 16

//#define USE_MULTI_ALLOCATOR  1
#define CHECK_MAGIC_NUMBER  1

#define PRIORITY_COUNT      3   //priority queue count

//statistic marco defines
//#define TRIGGER_STAT_FLAG  1  //trigger statistic flag
//#define MSG_TIME_STAT_FLAG 1  //data statistic flag

#define MSG_HEADER_LENGTH   ((int)sizeof(MsgHeader))
#define MAGIC_NUMBER        0x3308
#define MAX_MSG_LENGTH      (4 * 1024 * 1024)

#define MAX_MACHINE_COUNT        255   //IMPORTANT: can't be 256!!

//combine multi msg to call writev
#define WRITEV_ARRAY_SIZE   128
#define WRITEV_ITEM_ONCE    (WRITEV_ARRAY_SIZE / 2)
#define WRITE_MAX_COMBINE_BYTES  (64 * 1024)

#define CONNECT_TYPE_CLIENT  'C'  //connect by me, client
#define CONNECT_TYPE_SERVER  'S'  //connect by peer, server

#define DATA_TYPE_BUFFER     'B'  //char buffer
#define DATA_TYPE_OBJECT     'O'  //IOBufferBlock pointer

#define ALIGN_BYTES  8
#define BYTE_ALIGN(x,l)  (((x) + ((l) - 1)) & ~((l) - 1))
#define BYTE_ALIGN8(x)  BYTE_ALIGN(x, ALIGN_BYTES)

#define IS_SESSION_EMPTY(session_id) \
  ((session_id).ids[0] == 0 && (session_id).ids[1] == 0)

#define IS_SESSION_EQUAL(session_id1, session_id2) \
  ((session_id1).ids[0] == (session_id2).ids[0] && \
   (session_id1).ids[1] == (session_id2).ids[1])

typedef struct msg_timeused {
  volatile int64_t count;     //message count
  volatile int64_t time_used; //time used
} MsgTimeUsed;

typedef struct session_stat {
  volatile int64_t create_total_count;   //create session total count
  volatile int64_t create_success_count; //create session success count
  volatile int64_t create_retry_times;   //create session retry times
  volatile int64_t close_total_count;    //close session count
  volatile int64_t close_success_count;  //close session success count
  volatile int64_t session_miss_count;     //session miss count
  volatile int64_t session_occupied_count; //session occupied count
} SessionStat;

typedef struct msg_header {
#ifdef CHECK_MAGIC_NUMBER
  short magic;            //magic number
  unsigned short msg_seq; //message sequence no base 1
#else
  uint32_t msg_seq; //message sequence no base 1
#endif

  int func_id; //function id, must be signed int
  int data_len; //message body length
  int aligned_data_len;  //aligned body length
  SessionId session_id; //session id
} MsgHeader;   //must aligned by 8 bytes

typedef struct in_msg_entry {
  int func_id;  //function id
  int data_len; //message body length
  Ptr<IOBufferBlock> blocks;
  struct in_msg_entry *next; //for income message queue
} InMessage;

struct worker_thread_context;
struct socket_context;

typedef struct session_entry {
  SessionId session_id;
  void *user_data;  //user data for callback
  struct socket_context *sock_context;
  InMessage *messages;  //income messages
  int16_t response_events;  //response events
  uint16_t current_msg_seq;  //current message sequence no
  uint32_t version;    //avoid CAS ABA
  struct session_entry *next;  //session chain, only for server session

#ifdef TRIGGER_STAT_FLAG
  volatile int64_t stat_start_time;   //for message time used stat
#endif

#ifdef MSG_TIME_STAT_FLAG
  volatile int64_t client_start_time;  //send start time for client
  volatile int64_t server_start_time;  //recv done time for server
  volatile int64_t send_start_time; //send start time for stat send time
#endif

} SessionEntry;

//out message to send
typedef struct out_msg_entry {
  MsgHeader header;
  char mini_buff[MINI_MESSAGE_SIZE];  //for mini message
  Ptr<IOBufferBlock> blocks;  //block data passed by caller

	struct out_msg_entry *next; //for send queue
	int bytes_sent;    //important: including msg header
  int data_type;     //DATA_TYPE_BUFFER or DATA_TYPE_OBJECT
  int64_t in_queue_time; //the time when push to send queue
} OutMessage;

//out message queue
typedef struct message_queue {
  OutMessage *head;
  OutMessage *tail;
  ink_mutex lock;
} MessageQueue;

//for recv messages
typedef struct reader_manager {
  Ptr<IOBufferData> buffer;   //recv buffer
  Ptr<IOBufferBlock> blocks;  //recv blocks
  char *msg_header; //current message start
  char *current;    //current pointer
  char *buff_end;   //buffer end
  int recv_body_bytes;  //recveived body bytes
} ReaderManager;

typedef struct socket_context {
  int sock;  //socket fd
  char padding[ALIGN_BYTES];     //padding buffer
  struct reader_manager reader;  //recv buffer
  struct ClusterMachine *machine;     //peer machine, point to global machine
  struct worker_thread_context *thread_context; //the thread belong to
  MessageQueue send_queues[PRIORITY_COUNT];  //queue for send

  int queue_index;  //current deal queue index base 0
  int connect_type;       //CONNECT_TYPE_CLIENT or CONNECT_TYPE_SERVER
  time_t connected_time;  //connection established timestamp
  uint32_t version;    //avoid CAS ABA

  int64_t next_write_time; //next time to send message

  int ping_fail_count;     //cluster ping fail counter
  int64_t next_ping_time;  //next time to send ping message
  int64_t ping_start_time; //the start time of ping

#ifdef USE_MULTI_ALLOCATOR
  Allocator *out_msg_allocator;  //for send
  Allocator *in_msg_allocator;   //for notify dealer
#endif
  struct socket_context *next;  //for freelist
} SocketContext;

typedef struct socket_stats {
  int64_t send_msg_count;  //send msg count
  int64_t drop_msg_count;  //droped msg count when close socket
  int64_t send_bytes;
  int64_t drop_bytes;
  int64_t call_writev_count;
  int64_t send_retry_count;
  int64_t send_delayed_time;

  volatile int64_t push_msg_count; //push to send queue msg count
  volatile int64_t push_msg_bytes; //push to send queue msg bytes

  volatile int64_t fail_msg_count; //push to send queue fail msg count
  volatile int64_t fail_msg_bytes; //push to send queue fail msg bytes

  int64_t recv_msg_count;     //recv msg count
  int64_t enqueue_in_msg_count;  //push into in msg queue
  int64_t dequeue_in_msg_count;  //pop from in msg queue
  int64_t recv_bytes;
  int64_t enqueue_in_msg_bytes; //push into in msg queue
  int64_t dequeue_in_msg_bytes; //pop from in msg queue

  int64_t call_read_count;
  int64_t epoll_wait_count;
  int64_t epoll_wait_time_used;
  int64_t loop_usleep_count;
  int64_t loop_usleep_time;

  int64_t ping_total_count;
  int64_t ping_success_count;
  int64_t ping_time_used;
} SocketStats;

class EventPoll;

typedef struct worker_thread_context
{
  EventPoll *ev_poll;
  int alloc_size;         //max count of epoll events
  int thread_index;       //my thread index
  int active_sock_count;
  SocketStats stats;
  ink_mutex lock;
  SocketContext **active_sockets;
} WorkerThreadContext;

#endif

