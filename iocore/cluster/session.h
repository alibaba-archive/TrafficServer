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

#ifndef _SESSION_H_
#define _SESSION_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "clusterinterface.h"

typedef struct {
  unsigned int ip;
  bool init_done;
  bool is_myself;   //myself, the local host
  SessionEntry *sessions;
  ink_mutex *locks;
  volatile SequenceType current_seq;
  volatile SessionStat session_stat;

#ifdef TRIGGER_STAT_FLAG
  volatile MsgTimeUsed trigger_stat;
#endif

#ifdef MSG_TIME_STAT_FLAG
  volatile MsgTimeUsed msg_stat;
  volatile MsgTimeUsed msg_send;
#endif

} MachineSessions;

#define SESSION_LOCK(pMachineSessions, session_index) \
	ink_mutex_acquire((pMachineSessions)->locks + session_index % \
      session_lock_count_per_machine)

#define SESSION_UNLOCK(pMachineSessions, session_index) \
	ink_mutex_release((pMachineSessions)->locks + session_index % \
      session_lock_count_per_machine)

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

void log_session_stat();

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

