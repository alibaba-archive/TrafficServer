//session.h

#ifndef _SESSION_H_
#define _SESSION_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "types.h"
#include "clusterinterface.h"

typedef struct {
  unsigned int ip;
  bool init_done;
  bool is_myself;   //myself, the local host
  SessionEntry *sessions;
	pthread_mutex_t *locks;
  volatile SequenceType current_seq;

#ifdef SESSION_STAT_FLAG
  volatile SessionStat session_stat;
#endif

#ifdef TRIGGER_STAT_FLAG
  volatile MsgTimeUsed trigger_stat;
#endif

#ifdef MSG_TIME_STAT_FLAG
  volatile MsgTimeUsed msg_stat;
  volatile MsgTimeUsed msg_send;
#endif

} MachineSessions;

#define SESSION_LOCK(pMachineSessions, session_index) \
	pthread_mutex_lock((pMachineSessions)->locks + session_index % \
      SESSION_LOCK_COUNT_PER_MACHINE)

#define SESSION_UNLOCK(pMachineSessions, session_index) \
	pthread_mutex_unlock((pMachineSessions)->locks + session_index % \
      SESSION_LOCK_COUNT_PER_MACHINE)

#ifdef __cplusplus
extern "C" {
#endif

int session_init();
int init_machine_sessions(ClusterMachine *machine, const bool bMyself);

int get_session_for_send(const SessionId *session,
    MachineSessions **ppMachineSessions, SessionEntry **sessionEntry);
int get_response_session(const MsgHeader *pHeader,
    MachineSessions **ppMachineSessions, SessionEntry **sessionEntry,
    SocketContext *pSocketContext, bool *call_func, void **user_data);

int notify_connection_closed(SocketContext *pSockContext);

int push_in_message(const SessionId session,
    MachineSessions *pMachineSessions, SessionEntry *pSessionEntry,
    const int func_id, IOBufferBlock *blocks, const int data_len);

#ifdef SESSION_STAT_FLAG
void log_session_stat();
#endif

#ifdef TRIGGER_STAT_FLAG
void log_trigger_stat();
#endif

#ifdef MSG_TIME_STAT_FLAG
int get_response_session_internal(const MsgHeader *pHeader,
    MachineSessions **ppMachineSessions, SessionEntry **sessionEntry);
void log_msg_time_stat();
#endif

#ifdef __cplusplus
}
#endif

#endif

