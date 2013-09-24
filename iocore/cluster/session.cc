//session.c

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
#include "machine.h"
#include "global.h"
#include "connection.h"
#include "clusterinterface.h"
#include "nio.h"
#ifndef TS_INLINE
#define TS_INLINE inline
#endif
#include "I_IOBuffer.h"
#include "P_Cluster.h"
#include "P_RecCore.h"
#include "session.h"

#ifndef USE_MULTI_ALLOCATOR
static Allocator in_message_allocator("InMessage", sizeof(InMessage), 1024);
#endif

static Allocator session_allocator("SessionEntry", sizeof(SessionEntry), 1024);

static MachineSessions *all_sessions;  //[src ip % MAX_MACHINE_COUNT]
static ink_mutex session_lock;
static int my_machine_id = 0;

struct SessionRecords {
  RecRecord * create_total_count;   //create session total count
  RecRecord * create_success_count; //create session success count
  RecRecord * create_retry_times;   //create session retry times
  RecRecord * close_total_count;    //close session count
  RecRecord * close_success_count;  //close session success count
  RecRecord * session_miss_count;     //session miss count
  RecRecord * session_occupied_count; //session occupied count
};

static SessionRecords server_session_records = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static SessionRecords client_session_records = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};

static void init_session_stat(SessionRecords *pSessionRecords, const char *prefix);

inline static int get_session_machine_index(const unsigned int ip)
{
  int id;
  int count;
  int index;

  id = ip % MAX_MACHINE_COUNT;
  if (all_sessions[id].ip == ip) {
    return id;
  }

  count = 1;
  while (count <= MAX_MACHINE_COUNT) {
    index = (id + count) % MAX_MACHINE_COUNT;
    if (all_sessions[index].ip == ip) {
      return index;
    }
    count++;
  }

  return -1;
}

static int alloc_session_machine_index(const unsigned int ip)
{
  int id;
  int count;
  int index;

  id = ip % MAX_MACHINE_COUNT;
  if (all_sessions[id].ip == 0) {
    return id;
  }

  count = 1;
  while (count <= MAX_MACHINE_COUNT) {
    index = (id + count) % MAX_MACHINE_COUNT;
    if (all_sessions[index].ip == 0) {
      return index;
    }
    count++;
  }

  return -1;
}

inline static void release_in_message(SocketContext *pSockContext,
    InMessage *pMessage)
{
  pMessage->blocks = NULL;  //free pointer
#ifdef USE_MULTI_ALLOCATOR
  pSockContext->in_msg_allocator->free_void(pMessage);
#else
  in_message_allocator.free_void(pMessage);
#endif
}

int init_machine_sessions(ClusterMachine *machine, const bool bMyself)
{
  int result;
  int sessions_bytes;
  int locks_bytes;
  int machine_id;
  MachineSessions *pMachineSessions;
  ink_mutex *pLock;
  ink_mutex *pLockEnd;

  ink_mutex_acquire(&session_lock);
  if ((machine_id=get_session_machine_index(machine->ip)) < 0) {
    if ((machine_id=alloc_session_machine_index(machine->ip)) < 0) {
      ink_mutex_release(&session_lock);
      return ENOSPC;
    }
  }

  pMachineSessions = all_sessions + machine_id;
  if (pMachineSessions->init_done) {  //already init
    ink_mutex_release(&session_lock);
    return 0;
  }

  pMachineSessions->is_myself = bMyself;
  pMachineSessions->ip = machine->ip;

  sessions_bytes = sizeof(SessionEntry) * max_session_count_per_machine;
  pMachineSessions->sessions = (SessionEntry *)malloc(sessions_bytes);
  if (pMachineSessions->sessions == NULL) {
    Error("file: "__FILE__", line: %d, "
        "malloc %d bytes fail, errno: %d, error info: %s",
        __LINE__, sessions_bytes, errno, strerror(errno));
    ink_mutex_release(&session_lock);
    return errno != 0 ? errno : ENOMEM;
  }
  memset(pMachineSessions->sessions, 0, sessions_bytes);

  locks_bytes = sizeof(ink_mutex) * session_lock_count_per_machine;
  pMachineSessions->locks = (ink_mutex *)malloc(locks_bytes);
  if (pMachineSessions->locks == NULL) {
    Error("file: "__FILE__", line: %d, "
        "malloc %d bytes fail, errno: %d, error info: %s",
        __LINE__, locks_bytes, errno, strerror(errno));
    ink_mutex_release(&session_lock);
    return errno != 0 ? errno : ENOMEM;
  }

  pLockEnd = pMachineSessions->locks + session_lock_count_per_machine;
  for (pLock=pMachineSessions->locks; pLock<pLockEnd; pLock++) {
    if ((result=ink_mutex_init(pLock, "session_locks")) != 0) {
      ink_mutex_release(&session_lock);
      return result;
    }
  }

  pMachineSessions->init_done = true;
  ink_mutex_release(&session_lock);
  return 0;
}

int session_init()
{
  int bytes;
  int result;
  ClusterMachine *myMachine;

  bytes = sizeof(MachineSessions) * MAX_MACHINE_COUNT;
  all_sessions = (MachineSessions *)malloc(bytes);
  if (all_sessions == NULL) {
    Error("file: "__FILE__", line: %d, "
        "malloc %d bytes fail, errno: %d, error info: %s",
        __LINE__, bytes, errno, strerror(errno));
    return errno != 0 ? errno : ENOMEM;
  }
  memset(all_sessions, 0, bytes);

  myMachine = cluster_machines + 0;
  if ((result=init_machine_sessions(myMachine, true)) != 0) {
    return result;
  }

  if ((result=ink_mutex_init(&session_lock, "session_lock")) != 0) {
    return result;
  }

  my_machine_id = get_session_machine_index(myMachine->ip);
  Debug(CLUSTER_DEBUG_TAG, "my_machine_id: %d", my_machine_id);

  init_session_stat(&server_session_records, "proxy.process.cluster.server_session");
  init_session_stat(&client_session_records, "proxy.process.cluster.client_session");

  return 0;
}

int cluster_create_session(ClusterSession *session,
    const ClusterMachine *machine, void *arg, const int events)
{
  MachineSessions *pMachineSessions;
  SessionEntry *pSessionEntry;
  SocketContext *pSockContext;
  int i;
  int session_index;
  SequenceType seq;

  pMachineSessions = all_sessions + my_machine_id;

  ink_atomic_increment64(&pMachineSessions->session_stat.create_total_count, 1);

  if ((pSockContext=get_socket_context(machine)) == NULL) {
    return ENOENT;
  }

  for (i=0; i<128; i++) {
    seq = ink_atomic_increment64(&pMachineSessions->current_seq, 1);
    session_index = seq % max_session_count_per_machine;
    pSessionEntry = pMachineSessions->sessions + session_index;
    if (IS_SESSION_EMPTY(pSessionEntry->session_id)) {
      SESSION_LOCK(pMachineSessions, session_index);
      if (IS_SESSION_EMPTY(pSessionEntry->session_id)) {
        pSessionEntry->session_id.fields.ip = my_machine_ip;
        pSessionEntry->session_id.fields.timestamp = CURRENT_TIME();
        pSessionEntry->session_id.fields.seq = seq;
        pSessionEntry->sock_context = pSockContext;
        pSessionEntry->user_data = arg;
        pSessionEntry->response_events = events;
        pSessionEntry->current_msg_seq = 0;

        *session = pSessionEntry->session_id;

#ifdef TRIGGER_STAT_FLAG
        if (pSessionEntry->response_events & RESPONSE_EVENT_NOTIFY_DEALER) {
          pSessionEntry->stat_start_time = CURRENT_NS();
        }
#endif
        SESSION_UNLOCK(pMachineSessions, session_index);

        ink_atomic_increment64(&pMachineSessions->session_stat.
            create_success_count, 1);
        ink_atomic_increment64(&pMachineSessions->session_stat.
            create_retry_times, i + 1);
        return 0;
      }
      SESSION_UNLOCK(pMachineSessions, session_index);
    }
  }

  ink_atomic_increment64(&pMachineSessions->session_stat.
      create_retry_times, i);

  return ENOSPC;
}

#define GET_MACHINE_INDEX(machine_id, ip, pMachineSessions, return_value) \
  do { \
    if ((machine_id=get_session_machine_index(ip)) < 0) { \
      Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
          "ip: %u not exist!", __LINE__, ip); \
      return return_value; \
    } \
    pMachineSessions = all_sessions + machine_id; \
    if (!(pMachineSessions)->init_done) { \
      Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, " \
          "ip: %u not init!", __LINE__, ip); \
      return return_value; \
    } \
  } while (0)


inline static SessionEntry *get_session(
    const ClusterSession *session_id, SessionEntry *pSession)
{
  SessionEntry *pCurrent;
  pCurrent = pSession;
  do {
    if (IS_SESSION_EQUAL(pCurrent->session_id, *session_id)) {
      return pCurrent;
    }

    pCurrent = pCurrent->next;
  } while (pCurrent != NULL);

  return NULL;
}

int cluster_bind_session(ClusterSession session, void *arg)
{
  SessionEntry *pSessionEntry;
  MachineSessions *pMachineSessions;
  int result;
  int machine_id;
  int session_index;

  GET_MACHINE_INDEX(machine_id, session.fields.ip, pMachineSessions, ENOENT);

  session_index = session.fields.seq % max_session_count_per_machine;
  pSessionEntry = pMachineSessions->sessions + session_index;
  SESSION_LOCK(pMachineSessions, session_index);
  if ((pSessionEntry=get_session(&session, pSessionEntry)) != NULL) {
    pSessionEntry->user_data = arg;
    result = 0;
  }
  else {
    result = ENOENT;
  }
  SESSION_UNLOCK(pMachineSessions, session_index);
  return result;
}

int cluster_set_events(ClusterSession session, const int events)
{
  SessionEntry *pSessionEntry;
  MachineSessions *pMachineSessions;
  SocketContext *pSockContext;
  InMessage *pMessage;
  void *user_data;
  int result;
  int machine_id;
  int session_index;

  GET_MACHINE_INDEX(machine_id, session.fields.ip, pMachineSessions, ENOENT);

  session_index = session.fields.seq % max_session_count_per_machine;
  pSessionEntry = pMachineSessions->sessions + session_index;
  SESSION_LOCK(pMachineSessions, session_index);

  if ((pSessionEntry=get_session(&session, pSessionEntry)) != NULL) {
    pSockContext = pSessionEntry->sock_context;
    if (pSockContext != NULL) {
      if (events & RESPONSE_EVENT_NOTIFY_DEALER) {

        //assert((pSessionEntry->response_events & RESPONSE_EVENT_NOTIFY_DEALER) == 0);

#ifdef TRIGGER_STAT_FLAG
        //for stat
        if (pMachineSessions->is_myself) {  //client
          pSessionEntry->stat_start_time = CURRENT_NS();
        }
        else { //server
          if (pSessionEntry->stat_start_time != 0) {
            ink_atomic_increment64(&pMachineSessions->trigger_stat.count, 1);
            ink_atomic_increment64(&pMachineSessions->trigger_stat.time_used,
                CURRENT_NS() - pSessionEntry->stat_start_time);
            pSessionEntry->stat_start_time = 0;
          }
        }
#endif

        pMessage = pSessionEntry->messages;
        if (pMessage == NULL) {
          pSessionEntry->response_events = events;  //waiting for message to notify
        }
        else {
          pSessionEntry->messages = pSessionEntry->messages->next; //consume one
        }
      }
      else {
        pMessage = NULL;
        pSessionEntry->response_events = events;
      }

      user_data = pSessionEntry->user_data;
      result = 0;
    }
    else {
      pMessage = NULL;
      user_data = NULL;
      result = ENOENT;
    }
  }
  else {
    pSockContext = NULL;
    pMessage = NULL;
    user_data = NULL;
    result = ENOENT;
  }

#ifdef TRIGGER_STAT_FLAG
  if (pMessage != NULL) {
    if (!pMachineSessions->is_myself) {  //server
      pSessionEntry->stat_start_time = CURRENT_NS();
    }
  }
#endif
  SESSION_UNLOCK(pMachineSessions, session_index);

  if (pMessage != NULL) {
    cluster_msg_deal_func(session, user_data, pMessage->func_id,
        pMessage->blocks, pMessage->data_len);
    release_in_message(pSockContext, pMessage);
  }

  return result;
}

void *cluster_close_session(ClusterSession session)
{
  void *old_data;
  SessionEntry *previous;
  SessionEntry *pSessionEntry;
  MachineSessions *pMachineSessions;
  InMessage *pMessage;
  int machine_id;
  int session_index;

  GET_MACHINE_INDEX(machine_id, session.fields.ip, pMachineSessions, NULL);

  ink_atomic_increment64(&pMachineSessions->session_stat.close_total_count, 1);

  session_index = session.fields.seq % max_session_count_per_machine;
  pSessionEntry = pMachineSessions->sessions + session_index;
  SESSION_LOCK(pMachineSessions, session_index);

  previous = NULL;
  do {
    if (pSessionEntry->sock_context != NULL && IS_SESSION_EQUAL(
          session, pSessionEntry->session_id))
    {
      break;
    }

    previous = pSessionEntry;
    pSessionEntry = pSessionEntry->next;
  } while (pSessionEntry != NULL);

  if (pSessionEntry != NULL) {  //found
    old_data = pSessionEntry->user_data;
    while (pSessionEntry->messages != NULL) {
      pMessage = pSessionEntry->messages;
      pSessionEntry->messages = pSessionEntry->messages->next;

      release_in_message(pSessionEntry->sock_context, pMessage);
    }
    pSessionEntry->sock_context = NULL;
    pSessionEntry->response_events = 0;
    pSessionEntry->user_data = NULL;
    CLEAR_SESSION(pSessionEntry->session_id);

#ifdef TRIGGER_STAT_FLAG
    if (pSessionEntry->stat_start_time != 0) {
      ink_atomic_increment64(&pMachineSessions->trigger_stat.count, 1);
      ink_atomic_increment64(&pMachineSessions->trigger_stat.time_used,
          CURRENT_NS() - pSessionEntry->stat_start_time);
      pSessionEntry->stat_start_time = 0;
    }
#endif

    ink_atomic_increment64(&pMachineSessions->session_stat.
        close_success_count, 1);

#ifdef MSG_TIME_STAT_FLAG
    if (pMachineSessions->is_myself)
    {//request by me
      if (pSessionEntry->client_start_time != 0) {
        ink_atomic_increment64(&pMachineSessions->msg_stat.count, 1);
        ink_atomic_increment64(&pMachineSessions->msg_stat.time_used,
            CURRENT_NS() - pSessionEntry->client_start_time);
        pSessionEntry->client_start_time = 0;
      }
    }
    else { //request by other
      if (pSessionEntry->server_start_time != 0) {
        ink_atomic_increment64(&pMachineSessions->msg_stat.count, 1);
        ink_atomic_increment64(&pMachineSessions->msg_stat.time_used,
            CURRENT_NS() - pSessionEntry->server_start_time);
        pSessionEntry->server_start_time = 0;
      }
    }

    if (pSessionEntry->send_start_time != 0) {
      ink_atomic_increment64(&pMachineSessions->msg_send.count, 1);
      ink_atomic_increment64(&pMachineSessions->msg_send.time_used,
          CURRENT_NS() - pSessionEntry->send_start_time);
      pSessionEntry->send_start_time = 0;
    }
#endif

    if (previous == NULL) {  //remove the head session
      SessionEntry *pNextSession;
      pNextSession = pSessionEntry->next;
      if (pNextSession != NULL) {
        memcpy(pSessionEntry, pNextSession, sizeof(SessionEntry));
        session_allocator.free_void(pNextSession);
      }
    }
    else {
      previous->next = pSessionEntry->next;
      session_allocator.free_void(pSessionEntry);
    }
  }
  else {
    old_data = NULL;
  }
  SESSION_UNLOCK(pMachineSessions, session_index);
  return old_data;
}

int get_session_for_send(const SessionId *session,
    MachineSessions **ppMachineSessions, SessionEntry **sessionEntry)
{
  int machine_id;
  int session_index;
  int result;

  GET_MACHINE_INDEX(machine_id, session->fields.ip, *ppMachineSessions, ENOENT);

  session_index = session->fields.seq % max_session_count_per_machine;
  *sessionEntry = (*ppMachineSessions)->sessions + session_index;
  SESSION_LOCK(*ppMachineSessions, session_index);

  if ((*sessionEntry=get_session(session, *sessionEntry)) == NULL) {
    result = ENOENT;
  }
  else if ((*sessionEntry)->messages != NULL) {   //you must consume the recv messages firstly
    *sessionEntry = NULL;
    result = EBUSY;
  }
  else {
    result = 0;
  }

  SESSION_UNLOCK(*ppMachineSessions, session_index);
  return result;
}

#ifdef MSG_TIME_STAT_FLAG
int get_response_session_internal(const MsgHeader *pHeader,
    MachineSessions **ppMachineSessions, SessionEntry **sessionEntry)
{
  SessionEntry *pSession;
  SessionEntry *pCurrent;
  int result;
  int machine_id;
  int session_index;

  GET_MACHINE_INDEX(machine_id, pHeader->session_id.fields.ip,
      *ppMachineSessions, ENOENT);

  session_index = pHeader->session_id.fields.seq % max_session_count_per_machine;
  pSession = (*ppMachineSessions)->sessions + session_index;
  SESSION_LOCK(*ppMachineSessions, session_index);
  pCurrent = pSession;
  do {
    if (IS_SESSION_EQUAL(pCurrent->session_id, pHeader->session_id)) {
      *sessionEntry = pCurrent;
      result = 0;
      break;
    }

    pCurrent = pCurrent->next;
  } while (pCurrent != NULL);

  if (pCurrent == NULL) {
    if ((*ppMachineSessions)->is_myself) { //request by me
      *sessionEntry = NULL;
      result = ENOENT;
    }
    else {
      if (IS_SESSION_EMPTY(pSession->session_id)) {
        if (pHeader->msg_seq == 1) {  //first time, should create
          *sessionEntry = pSession;
          result = 0;
        }
        else {
          *sessionEntry = NULL;
          result = ENOENT;
        }
      }
      else {
        *sessionEntry = NULL;
        result = EEXIST;
      }
    }
  }

  SESSION_UNLOCK(*ppMachineSessions, session_index);
  return result;
}
#endif

int get_response_session(const MsgHeader *pHeader,
    MachineSessions **ppMachineSessions, SessionEntry **sessionEntry,
    SocketContext *pSocketContext, bool *call_func, void **user_data)
{
  SessionEntry *pSession;
  SessionEntry *pTail;
  SessionEntry *pCurrent;
  int result;
  int machine_id;
  int session_index;
  int chain_count;

  GET_MACHINE_INDEX(machine_id, pHeader->session_id.fields.ip,
      *ppMachineSessions, ENOENT);

  session_index = pHeader->session_id.fields.seq % max_session_count_per_machine;
  pSession = (*ppMachineSessions)->sessions + session_index;
  SESSION_LOCK(*ppMachineSessions, session_index);
  do {
    pCurrent = pSession;
    do {
      if (IS_SESSION_EQUAL(pCurrent->session_id, pHeader->session_id)) {
        *sessionEntry = pCurrent;
        *user_data = pCurrent->user_data;
        result = 0;

        if (pCurrent->response_events & RESPONSE_EVENT_NOTIFY_DEALER) {
          pCurrent->response_events = 0;
          *call_func = true;
        }
        else {
          *call_func = false;
        }

        break;
      }

      pCurrent = pCurrent->next;
    } while (pCurrent != NULL);

    if (pCurrent != NULL) {  //found
      pSession = pCurrent;
      break;
    }

    if ((*ppMachineSessions)->is_myself) { //request by me
      if (IS_SESSION_EMPTY(pSession->session_id)) {
        Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, "
            "client sessionEntry: %16lX:%lX not exist, func_id: %d",
            __LINE__, pHeader->session_id.ids[0],
            pHeader->session_id.ids[1], pHeader->func_id);
        *sessionEntry = NULL;
        *call_func = false;
        *user_data = NULL;
        result = ENOENT;

        ink_atomic_increment64(&(*ppMachineSessions)->session_stat.
            session_miss_count, 1);
        break;
      }
    }
    else {  //request by other
      if (pHeader->msg_seq > 1) {   //should discard the message
        *sessionEntry = NULL;
        *user_data = NULL;
        *call_func = false;
        result = ENOENT;

        Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, "
            "server sessionEntry: %08X:%u:%"PRId64" not exist, msg seq: %u, "
            "func_id: %d, data_len: %d",
            __LINE__, pHeader->session_id.fields.ip,
            pHeader->session_id.fields.timestamp,
            pHeader->session_id.ids[1], pHeader->msg_seq,
            pHeader->func_id, pHeader->data_len);

        ink_atomic_increment64(&(*ppMachineSessions)->session_stat.
            session_miss_count, 1);
        break;
      }

      if (IS_SESSION_EMPTY(pSession->session_id)) {
        pTail = NULL;
        chain_count = 0;
      }
      else {
        chain_count = 1;
        pTail = pSession;
        if (pSession->next != NULL) {
          ++chain_count;
          pTail = pSession->next;
          pSession = pTail->next;
          while (pSession != NULL) {
            pTail = pSession;
            pSession = pSession->next;
            ++chain_count;
          }
        }

        pSession = (SessionEntry *)session_allocator.alloc_void();
        pSession->messages = NULL;
        pSession->user_data = NULL;
        pSession->next = NULL;

#ifdef TRIGGER_STAT_FLAG
        pSession->stat_start_time = 0;
#endif
#ifdef MSG_TIME_STAT_FLAG
        pSession->client_start_time = 0;
        pSession->server_start_time = 0;
        pSession->send_start_time = 0;
#endif
      }

      //first time, should create
      pSession->session_id = pHeader->session_id;  //set sessionEntry id
      pSession->sock_context = pSocketContext;
      pSession->response_events = 0;
      pSession->current_msg_seq = 0;
      if (pTail != NULL) {
        pTail->next = pSession;

        Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, "
            "sessionEntry: %08X:%u:%"PRId64", chain count: %d",
            __LINE__, pHeader->session_id.fields.ip,
            pHeader->session_id.fields.timestamp,
            pHeader->session_id.ids[1], chain_count + 1);
      }

      *sessionEntry = pSession;
      *user_data = NULL;
      *call_func = true;
      result = 0;

      ink_atomic_increment64(&(*ppMachineSessions)->session_stat.
          create_total_count, 1);
      break;
    }

    Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, "
        "sessionEntry: %08X:%u:%"PRId64", position occupied by %08X:%u:%"PRId64", "
        "quest by me: %d, time distance: %u, func_id: %d",
        __LINE__, pHeader->session_id.fields.ip,
        pHeader->session_id.fields.timestamp, pHeader->session_id.ids[1],
        pSession->session_id.fields.ip, pSession->session_id.fields.timestamp,
        pSession->session_id.ids[1], machine_id == my_machine_id,
        pHeader->session_id.fields.timestamp -
        pSession->session_id.fields.timestamp, pHeader->func_id);
    *sessionEntry = NULL;
    *user_data = NULL;
    *call_func = false;
    result = EEXIST;

    ink_atomic_increment64(&(*ppMachineSessions)->session_stat.
        session_occupied_count, 1);
  } while (0);

#ifdef TRIGGER_STAT_FLAG
  if (*call_func) {
    //stat
    if ((*ppMachineSessions)->is_myself) { //request by me
      if (pSession->stat_start_time != 0) {
        ink_atomic_increment64(&(*ppMachineSessions)->trigger_stat.count, 1);
        ink_atomic_increment64(&(*ppMachineSessions)->trigger_stat.time_used,
            CURRENT_NS() - pSession->stat_start_time);
        pSession->stat_start_time = 0;
      }
    }
    else {
      pSession->stat_start_time = CURRENT_NS();
    }
  }
#endif

  SESSION_UNLOCK(*ppMachineSessions, session_index);
  return result;
}

static int do_notify_connection_closed(const int src_machine_id,
    SocketContext *pSockContext)
{
  int count;
  int session_index;
  SessionEntry *pcurrent;
  SessionEntry *pSessionEntry;
  SessionEntry *pSessionEnd;
  void *user_data;
  bool call_func;
  SessionId session_id;

  count = 0;
  pSessionEnd = all_sessions[src_machine_id].sessions +
    max_session_count_per_machine;
  for (pSessionEntry=all_sessions[src_machine_id].sessions;
      pSessionEntry<pSessionEnd; pSessionEntry++)
  {
    pcurrent = pSessionEntry;
    do {
      if (pcurrent->sock_context == pSockContext) {
        session_index = pSessionEntry - all_sessions[src_machine_id].sessions;
        SESSION_LOCK(all_sessions + src_machine_id, session_index);
        call_func = (pcurrent->response_events &
            RESPONSE_EVENT_NOTIFY_DEALER) && (pcurrent->messages == NULL);
        session_id = pcurrent->session_id;
        user_data = pcurrent->user_data;
        SESSION_UNLOCK(all_sessions + src_machine_id, session_index);

        if (call_func) {
          cluster_msg_deal_func(session_id, user_data,
              FUNC_ID_CONNECTION_CLOSED_NOTIFY, NULL, 0);
        }
        else {
          push_in_message(session_id, all_sessions + src_machine_id,
              pcurrent, FUNC_ID_CONNECTION_CLOSED_NOTIFY, NULL, 0);
        }

        count++;
      }

      pcurrent = pcurrent->next;
    } while (pcurrent != NULL);
  }

  return count;
}

int notify_connection_closed(SocketContext *pSockContext)
{
  int count1;
  int count2;
  int machine_id;

  count1 = do_notify_connection_closed(my_machine_id, pSockContext);
  if (count1 > 0) {
    Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, "
        "notify my session close count: %d", __LINE__, count1);
  }

  machine_id = get_session_machine_index(pSockContext->machine->ip);
  if (machine_id >= 0 && all_sessions[machine_id].init_done) {
    count2 = do_notify_connection_closed(machine_id, pSockContext);
    if (count2 > 0) {
      Debug(CLUSTER_DEBUG_TAG, "file: "__FILE__", line: %d, "
          "notify %s session close count: %d", __LINE__,
          pSockContext->machine->hostname, count2);
    }
  }
  else {
    count2 = 0;
  }

  return count1 + count2;
}

int push_in_message(const SessionId session,
    MachineSessions *pMachineSessions, SessionEntry *pSessionEntry,
    const int func_id, IOBufferBlock *blocks, const int data_len)
{
  SocketContext *pSockContext;
  InMessage *pMessage;
  void *user_data;
  int session_index;
  bool call_func;

  session_index = session.fields.seq % max_session_count_per_machine;
  SESSION_LOCK(pMachineSessions, session_index);
  pSockContext = pSessionEntry->sock_context;
  if (!(pSockContext != NULL && IS_SESSION_EQUAL(pSessionEntry->session_id,
          session)))
  {
    SESSION_UNLOCK(pMachineSessions, session_index);
    return ENOENT;
  }

#ifdef USE_MULTI_ALLOCATOR
  pMessage = (InMessage *)pSockContext->in_msg_allocator->alloc_void();
#else
  pMessage = (InMessage *)in_message_allocator.alloc_void();
#endif

  if (pMessage == NULL) {
    Error("file: "__FILE__", line: %d, "
        "malloc %d bytes fail, errno: %d, error info: %s",
        __LINE__, (int)sizeof(InMessage), errno, strerror(errno));
    SESSION_UNLOCK(pMachineSessions, session_index);
    return errno != 0 ? errno : ENOMEM;
  }

  pMessage->blocks.m_ptr = NULL;  //must set to NULL before set value
  pMessage->func_id = func_id;
  pMessage->blocks = blocks;
  pMessage->data_len = data_len;
  pMessage->next = NULL;

  if (pSessionEntry->messages == NULL) {
    pSessionEntry->messages = pMessage;
  }
  else if (pSessionEntry->messages->next == NULL) {
    pSessionEntry->messages->next = pMessage;
  }
  else {
    InMessage *pTail;
    pTail = pSessionEntry->messages->next;
    while (pTail->next != NULL) {
      pTail = pTail->next;
    }
    pTail->next = pMessage;
  }

  //check if notify dealer
  if (pSessionEntry->response_events & RESPONSE_EVENT_NOTIFY_DEALER) {
    pSessionEntry->response_events = 0;
    pMessage = pSessionEntry->messages;
    pSessionEntry->messages = pSessionEntry->messages->next; //consume one
    user_data = pSessionEntry->user_data;
    call_func = true;
  }
  else {
    user_data = NULL;
    call_func = false;
  }

#ifdef TRIGGER_STAT_FLAG
  if (call_func) {
    if (!pMachineSessions->is_myself) {  //server
      pSessionEntry->stat_start_time = CURRENT_NS();
    }
  }
#endif
  SESSION_UNLOCK(pMachineSessions, session_index);

  if (call_func) {
    cluster_msg_deal_func(session, user_data, pMessage->func_id,
        pMessage->blocks, pMessage->data_len);

    release_in_message(pSockContext, pMessage);
  }

  return 0;
}

static void set_session_stat(SessionRecords *pSessionRecords,
    const SessionStat *pNewtat, SessionStat *pOldStat)
{
  if (pNewtat->create_total_count != pOldStat->create_total_count) {
    pOldStat->create_total_count = pNewtat->create_total_count;
    RecDataSetFromInk64(RECD_INT, &pSessionRecords->create_total_count->data,
        pNewtat->create_total_count);
  }
  if (pNewtat->create_success_count != pOldStat->create_success_count) {
    pOldStat->create_success_count = pNewtat->create_success_count;
    RecDataSetFromInk64(RECD_INT, &pSessionRecords->create_success_count->data,
        pNewtat->create_success_count);
  }
  if (pNewtat->create_retry_times != pOldStat->create_retry_times) {
    pOldStat->create_retry_times = pNewtat->create_retry_times;
    RecDataSetFromInk64(RECD_INT, &pSessionRecords->create_retry_times->data,
        pNewtat->create_retry_times);
  }
  if (pNewtat->close_total_count != pOldStat->close_total_count) {
    pOldStat->close_total_count = pNewtat->close_total_count;
    RecDataSetFromInk64(RECD_INT, &pSessionRecords->close_total_count->data,
        pNewtat->close_total_count);
  }
  if (pNewtat->close_success_count != pOldStat->close_success_count) {
    pOldStat->close_success_count = pNewtat->close_success_count;
    RecDataSetFromInk64(RECD_INT, &pSessionRecords->close_success_count->data,
        pNewtat->close_success_count);
  }
  if (pNewtat->session_miss_count != pOldStat->session_miss_count) {
    pOldStat->session_miss_count = pNewtat->session_miss_count;
    RecDataSetFromInk64(RECD_INT, &pSessionRecords->session_miss_count->data,
        pNewtat->session_miss_count);
  }
  if (pNewtat->session_occupied_count != pOldStat->session_occupied_count) {
    pOldStat->session_occupied_count = pNewtat->session_occupied_count;
    RecDataSetFromInk64(RECD_INT, &pSessionRecords->session_occupied_count->data,
        pNewtat->session_occupied_count);
  }
}

static void init_session_stat(SessionRecords *pSessionRecords, const char *prefix)
{
  char name[256];
  RecData data_default;
  memset(&data_default, 0, sizeof(RecData));

  sprintf(name, "%s.create_total_count", prefix);
  pSessionRecords->create_total_count = RecRegisterStat(RECT_PROCESS,
      name, RECD_INT, data_default, RECP_NON_PERSISTENT);

  sprintf(name, "%s.create_success_count", prefix);
  pSessionRecords->create_success_count = RecRegisterStat(RECT_PROCESS,
      name, RECD_INT, data_default, RECP_NON_PERSISTENT);

  sprintf(name, "%s.create_retry_times", prefix);
  pSessionRecords->create_retry_times = RecRegisterStat(RECT_PROCESS,
      name, RECD_INT, data_default, RECP_NON_PERSISTENT);

  sprintf(name, "%s.close_total_count", prefix);
  pSessionRecords->close_total_count = RecRegisterStat(RECT_PROCESS,
      name, RECD_INT, data_default, RECP_NON_PERSISTENT);

  sprintf(name, "%s.close_success_count", prefix);
  pSessionRecords->close_success_count = RecRegisterStat(RECT_PROCESS,
      name, RECD_INT, data_default, RECP_NON_PERSISTENT);

  sprintf(name, "%s.miss_count", prefix);
  pSessionRecords->session_miss_count = RecRegisterStat(RECT_PROCESS,
      name, RECD_INT, data_default, RECP_NON_PERSISTENT);

  sprintf(name, "%s.occupied_count", prefix);
  pSessionRecords->session_occupied_count = RecRegisterStat(RECT_PROCESS,
      name, RECD_INT, data_default, RECP_NON_PERSISTENT);
}


void log_session_stat()
{
  ClusterMachine *pMachine;
  ClusterMachine *pMachineEnd;
  int machine_id;
  MachineSessions *pServerSessions;
  MachineSessions *pClientSessions;
  SessionStat serverSessionStat;
  static SessionStat serverOldStat = {0, 0, 0, 0, 0, 0, 0};
  static SessionStat clientOldStat = {0, 0, 0, 0, 0, 0, 0};

  serverSessionStat.create_total_count = 0;
  serverSessionStat.create_success_count = 0;
  serverSessionStat.create_retry_times = 0;
  serverSessionStat.close_total_count = 0;
  serverSessionStat.close_success_count = 0;
  serverSessionStat.session_miss_count = 0;
  serverSessionStat.session_occupied_count = 0;

  pMachineEnd = cluster_machines + cluster_machine_count;
  for (pMachine=cluster_machines; pMachine<pMachineEnd; pMachine++) {
    if ((machine_id=get_session_machine_index(pMachine->ip)) < 0) {
      continue;
    }
    if (pMachine->dead || machine_id == my_machine_id) {
      continue;
    }

    pServerSessions = all_sessions + machine_id;
    serverSessionStat.create_total_count += pServerSessions->session_stat.
      create_total_count;
    serverSessionStat.close_total_count += pServerSessions->session_stat.
      close_total_count;
    serverSessionStat.close_success_count += pServerSessions->session_stat.
      close_success_count;
    serverSessionStat.session_miss_count += pServerSessions->session_stat.
      session_miss_count;
    serverSessionStat.session_occupied_count += pServerSessions->session_stat.
      session_occupied_count;
  }

  serverSessionStat.create_success_count = serverSessionStat.create_total_count;
  serverSessionStat.create_retry_times = serverSessionStat.create_total_count;

  pClientSessions = all_sessions + my_machine_id;

  set_session_stat(&server_session_records, &serverSessionStat, &serverOldStat);
  set_session_stat(&client_session_records, (const SessionStat *)
      &pClientSessions->session_stat, &clientOldStat);
}

#ifdef TRIGGER_STAT_FLAG
void log_trigger_stat()
{
  ClusterMachine *pMachine;
  ClusterMachine *pMachineEnd;
  MachineSessions *pServerSessions;
  MachineSessions *pClientSessions;
  MsgTimeUsed serverTimeUsed;
  int machine_id;
  int server_avg_time_used;
  int client_avg_time_used;

  serverTimeUsed.count = 0;
  serverTimeUsed.time_used = 0;

  pMachineEnd = cluster_machines + cluster_machine_count;
  for (pMachine=cluster_machines; pMachine<pMachineEnd; pMachine++) {
    if ((machine_id=get_session_machine_index(pMachine->ip)) < 0) {
      continue;
    }
    if (pMachine->dead || machine_id == my_machine_id) {
      continue;
    }

    pServerSessions = all_sessions + machine_id;

    serverTimeUsed.count += pServerSessions->trigger_stat.count;
    serverTimeUsed.time_used += pServerSessions->trigger_stat.time_used;
    if (pServerSessions->trigger_stat.count > 0) {
      server_avg_time_used = pServerSessions->trigger_stat.time_used /
        pServerSessions->trigger_stat.count;
    }
    else {
      server_avg_time_used = 0;
    }
    Note("%s:%d trigger msg => %"PRId64", avg time used => %d us",
        pMachine->hostname, pMachine->cluster_port,
        pServerSessions->trigger_stat.count,
        server_avg_time_used / 1000);

    pServerSessions->trigger_stat.count = 0;
    pServerSessions->trigger_stat.time_used = 0;
  }

  if (serverTimeUsed.count > 0) {
    server_avg_time_used = serverTimeUsed.time_used / serverTimeUsed.count;
  }
  else {
    server_avg_time_used = 0;
  }
  Note("SERVER: trigger msg => %"PRId64", avg time used => %d us",
      serverTimeUsed.count, server_avg_time_used / 1000);

  pClientSessions = all_sessions + my_machine_id;
  if (pClientSessions->trigger_stat.count > 0) {
    client_avg_time_used = pClientSessions->trigger_stat.time_used /
      pClientSessions->trigger_stat.count;
  }
  else {
    client_avg_time_used = 0;
  }
  Note("CLIENT: trigger msg => %"PRId64", avg time used => %d us\n",
      pClientSessions->trigger_stat.count, client_avg_time_used / 1000);

  pClientSessions->trigger_stat.count = 0;
  pClientSessions->trigger_stat.time_used = 0;
}
#endif

#ifdef MSG_TIME_STAT_FLAG
void log_msg_time_stat()
{
  ClusterMachine *pMachine;
  ClusterMachine *pMachineEnd;
  MachineSessions *pServerSessions;
  MachineSessions *pClientSessions;
  MsgTimeUsed serverTimeUsed;
  MsgTimeUsed sendTimeUsed;
  int machine_id;
  int server_avg_time_used;
  int client_avg_time_used;
  int send_avg_time_used;

  serverTimeUsed.count = 0;
  serverTimeUsed.time_used = 0;
  sendTimeUsed.count = 0;
  sendTimeUsed.time_used = 0;

  pMachineEnd = cluster_machines + cluster_machine_count;
  for (pMachine=cluster_machines; pMachine<pMachineEnd; pMachine++) {
    if ((machine_id=get_session_machine_index(pMachine->ip)) < 0) {
      continue;
    }
    if (pMachine->dead || machine_id == my_machine_id) {
      continue;
    }

    pServerSessions = all_sessions + machine_id;
    serverTimeUsed.count += pServerSessions->msg_stat.count;
    serverTimeUsed.time_used += pServerSessions->msg_stat.time_used;
    if (pServerSessions->msg_stat.count > 0) {
      server_avg_time_used = pServerSessions->msg_stat.time_used /
        pServerSessions->msg_stat.count;
    }
    else {
      server_avg_time_used = 0;
    }

    sendTimeUsed.count += pServerSessions->msg_send.count;
    sendTimeUsed.time_used += pServerSessions->msg_send.time_used;
    if (pServerSessions->msg_send.count > 0) {
      send_avg_time_used = pServerSessions->msg_send.time_used /
        pServerSessions->msg_send.count;
    }
    else {
      send_avg_time_used = 0;
    }

    Note("%s:%d msg count: %"PRId64", avg time used (recv start to send done): %d us, "
        "send msg count: %"PRId64", send avg time: %d us",
        pMachine->hostname, pMachine->cluster_port,
        pServerSessions->msg_stat.count, server_avg_time_used / 1000,
        pServerSessions->msg_send.count, send_avg_time_used / 1000);

    pServerSessions->msg_stat.count = 0;
    pServerSessions->msg_stat.time_used = 0;
    pServerSessions->msg_send.count = 0;
    pServerSessions->msg_send.time_used = 0;
  }

  if (serverTimeUsed.count > 0) {
    server_avg_time_used = serverTimeUsed.time_used / serverTimeUsed.count;
  }
  else {
    server_avg_time_used = 0;
  }

  if (sendTimeUsed.count > 0) {
    send_avg_time_used = sendTimeUsed.time_used / sendTimeUsed.count;
  }
  else {
    send_avg_time_used = 0;
  }
  Note("SERVER: msg count: %"PRId64", avg time used (recv start to send done): %d us, "
      "send msg count: %"PRId64", send avg time: %d us",
      serverTimeUsed.count, server_avg_time_used / 1000,
      sendTimeUsed.count, send_avg_time_used / 1000);

  pClientSessions = all_sessions + my_machine_id;
  if (pClientSessions->msg_stat.count > 0) {
    client_avg_time_used = pClientSessions->msg_stat.time_used /
      pClientSessions->msg_stat.count;
  }
  else {
    client_avg_time_used = 0;
  }
  if (pClientSessions->msg_send.count > 0) {
    send_avg_time_used = pClientSessions->msg_send.time_used /
      pClientSessions->msg_send.count;
  }
  else {
    send_avg_time_used = 0;
  }
  Note("CLIENT: msg count: %"PRId64", avg time used (send start to recv done): %d us, "
      "send msg count: %"PRId64", send avg time: %d us\n",
      pClientSessions->msg_stat.count, client_avg_time_used / 1000,
      pClientSessions->msg_send.count, send_avg_time_used / 1000);

  pClientSessions->msg_stat.count = 0;
  pClientSessions->msg_stat.time_used = 0;
  pClientSessions->msg_send.count = 0;
  pClientSessions->msg_send.time_used = 0;
}
#endif

