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

/****************************************************************************

  ClusterInline.h
****************************************************************************/

#ifndef __P_CLUSTERINLINE_H__
#define __P_CLUSTERINLINE_H__
#include "P_ClusterCacheInternal.h"
#include "P_CacheInternal.h"
#include "P_ClusterHandler.h"
#include "clusterinterface.h"

inline Action *
Cluster_lookup(Continuation * cont, CacheKey * key, CacheFragType frag_type, char *hostname, int host_len)
{
  // Try to send remote, if not possible, handle locally
//  Action *retAct;
//  ClusterMachine *m = cluster_machine_at_depth(cache_hash(*key));
//  if (m && !clusterProcessor.disable_remote_cluster_ops(m)) {
//    CacheContinuation *cc = CacheContinuation::cacheContAllocator_alloc();
//    cc->action = cont;
//    cc->mutex = cont->mutex;
//    retAct = CacheContinuation::do_remote_lookup(cont, key, cc, frag_type, hostname, host_len);
//    if (retAct) {
//      return retAct;
//    } else {
//      // not remote, do local lookup
//      CacheContinuation::cacheContAllocator_free(cc);
//      return (Action *) NULL;
//    }
//  } else {
//    Action a;
//    a = cont;
//    return CacheContinuation::callback_failure(&a, CACHE_EVENT_LOOKUP_FAILED, 0);
//  }
  return (Action *) NULL;
}

inline Action *
Cluster_read(ClusterMachine * owner_machine, int opcode,
             Continuation * cont, MIOBuffer * buf,
             CacheURL * url, CacheHTTPHdr * request,
             CacheLookupHttpConfig * params, CacheKey * key,
             time_t pin_in_cache, CacheFragType frag_type, char *hostname, int host_len)
{
  (void) params;
  ink_assert(cont);
  ClusterSession session;
  if (cluster_create_session(&session, owner_machine, NULL, 0)) {
    cont->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, NULL);
    return ACTION_RESULT_DONE;
  }

  int vers = CacheOpMsg_long::protoToVersion(owner_machine->msg_proto_major);
  CacheOpArgs_General readArgs;
  Ptr<IOBufferData> d = NULL;

  int flen;
  int len = 0;
  int cur_len;
  int res = 0;
  char *msg = 0;
  char *data;
  Action *action = NULL;

  if (vers == CacheOpMsg_long::CACHE_OP_LONG_MESSAGE_VERSION) {
    if ((opcode == CACHE_OPEN_READ_LONG)
        || (opcode == CACHE_OPEN_READ_BUFFER_LONG)) {
      // Determine length of data to Marshal
      flen = op_to_sizeof_fixedlen_msg(opcode);

      const char *url_hostname;
      int url_hlen;
      INK_MD5 url_md5;

      Cache::generate_key(&url_md5, url);
      url_hostname = url->host_get(&url_hlen);

      len += request->m_heap->marshal_length();
      len += sizeof(CacheLookupHttpConfig) + params->marshal_length();
      len += url_hlen;

      if ((flen + len) > DEFAULT_MAX_BUFFER_SIZE)       // Bound marshalled data
        goto err_exit;

      // Perform data Marshal operation
      d = new_IOBufferData(iobuffer_size_to_index(flen + len));
      msg = (char *) d->data();
      data = msg + flen;

      cur_len = len;
      res = request->m_heap->marshal(data, cur_len);
      if (res < 0) {
        goto err_exit;
      }
      data += res;
      cur_len -= res;

      if (cur_len < (int) sizeof(CacheLookupHttpConfig))
        goto err_exit;
      memcpy(data, params, sizeof(CacheLookupHttpConfig));
      data += sizeof(CacheLookupHttpConfig);
      cur_len -= sizeof(CacheLookupHttpConfig);

      if ((res = params->marshal(data, cur_len)) < 0)
        goto err_exit;
      data += res;
      cur_len -= res;
      memcpy(data, url_hostname, url_hlen);

      CacheOpArgs_General readArgs;
      readArgs.url_md5 = &url_md5;
      readArgs.pin_in_cache = pin_in_cache;
      readArgs.frag_type = frag_type;

      action = CacheContinuation::do_op(cont, session, (void *) &readArgs,
                                            opcode, d, (flen + len), -1, buf);
    } else {
      // Build message if we have host data.
      flen = op_to_sizeof_fixedlen_msg(opcode);
      len = host_len;

      if ((flen + len) > DEFAULT_MAX_BUFFER_SIZE)     // Bound marshalled data
        goto err_exit;

      d = new_IOBufferData(iobuffer_size_to_index(flen + len));
      msg = (char *) d->data();
      data = msg + flen;
      if (host_len)
        memcpy(data, hostname, host_len);

      readArgs.url_md5 = key;
      readArgs.frag_type = frag_type;

      action = CacheContinuation::do_op(cont, session, (void *) &readArgs,
                                            opcode, d, (flen + len), -1, buf);
    }
    ink_assert(msg);

  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"CacheOpMsg_long [read] bad msg version");
  }

  if (action)
    return action;
err_exit:
  cont->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, NULL);
  return ACTION_RESULT_DONE;
}

inline Action *
Cluster_write(Continuation * cont, int expected_size,
              MIOBuffer * buf, ClusterMachine * m,
              INK_MD5 * url_md5, CacheFragType ft, int options,
              time_t pin_in_cache, int opcode,
              CacheKey * key, CacheURL * url,
              CacheHTTPHdr * request, CacheHTTPInfo * old_info, char *hostname, int host_len)
{
  (void) key;
  (void) request;
  ClusterSession session;
  ink_assert(cont);
  if (cluster_create_session(&session, m, NULL, 0)) {
     cont->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, NULL);
     return ACTION_RESULT_DONE;
  }
  char *msg = 0;
  char *data = 0;
  int allow_multiple_writes = 0;
  int len = 0;
  int flen = 0;
  int vers = CacheOpMsg_long::protoToVersion(m->msg_proto_major);
  Ptr<IOBufferData> d = NULL;

  switch (opcode) {
  case CACHE_OPEN_WRITE:
    {
      // Build message if we have host data
      len = host_len;
      flen = op_to_sizeof_fixedlen_msg(CACHE_OPEN_WRITE);
      if ((flen + len) > DEFAULT_MAX_BUFFER_SIZE)     // Bound marshalled data
        goto err_exit;

      d = new_IOBufferData(iobuffer_size_to_index(flen + len));
      msg = (char *) d->data();
      data = msg + flen;
      if (host_len)
        memcpy(data, hostname, host_len);
      break;
    }
  case CACHE_OPEN_WRITE_LONG:
    {
      int url_hlen;
      const char *url_hostname = url->host_get(&url_hlen);

      // Determine length of data to Marshal
      flen = op_to_sizeof_fixedlen_msg(CACHE_OPEN_WRITE_LONG);
      len = 0;

      if (old_info == (CacheHTTPInfo *) CACHE_ALLOW_MULTIPLE_WRITES) {
        old_info = 0;
        allow_multiple_writes = 1;
      }
      if (old_info) {
        len += old_info->marshal_length();
      }
      len += url_hlen;

      if ((flen + len) > DEFAULT_MAX_BUFFER_SIZE)       // Bound marshalled data
        goto err_exit;

      d = new_IOBufferData(iobuffer_size_to_index(flen + len));
      msg = (char *) d->data();
      // Perform data Marshal operation
      data = msg + flen;
      int res = 0;

      int cur_len = len;

      if (old_info) {
        res = old_info->marshal(data, cur_len);
        if (res < 0) {
          goto err_exit;
        }
        data += res;
        cur_len -= res;
      }
      memcpy(data, url_hostname, url_hlen);
      break;
    }
  default:
    {
      ink_release_assert(!"open_write_internal invalid opcode.");
    }                           // End of case
  }                             // End of switch

  if (vers == CacheOpMsg_long::CACHE_OP_LONG_MESSAGE_VERSION) {
    // Do remote open_write()
    CacheOpArgs_General writeArgs;
    writeArgs.url_md5 = url_md5;
    writeArgs.pin_in_cache = pin_in_cache;
    writeArgs.frag_type = ft;
    writeArgs.cfl_flags |= (options & CACHE_WRITE_OPT_OVERWRITE ? CFL_OVERWRITE_ON_WRITE : 0);
    writeArgs.cfl_flags |= (old_info ? CFL_LOPENWRITE_HAVE_OLDINFO : 0);
    writeArgs.cfl_flags |= (allow_multiple_writes ? CFL_ALLOW_MULTIPLE_WRITES : 0);

    Action *action = CacheContinuation::do_op(cont, session, (void *) &writeArgs, opcode, d, flen + len, expected_size, buf);
    if (action)
      return action;
  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"CacheOpMsg_long [write] bad msg version");
    return (Action *) 0;
  }

err_exit:
  cont->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, NULL);
  return ACTION_RESULT_DONE;
}

inline Action *
Cluster_link(ClusterMachine * m, Continuation * cont, CacheKey * from, CacheKey * to,
             CacheFragType type, char *hostname, int host_len)
{
  ClusterSession session;
  Ptr<IOBufferData> d = NULL;
  char *msg = NULL;

  if (cluster_create_session(&session, m, NULL, 0)) {
    cont->handleEvent(CACHE_EVENT_LINK_FAILED, NULL);
    return ACTION_RESULT_DONE;
  }

  int vers = CacheOpMsg_short_2::protoToVersion(m->msg_proto_major);
  if (vers == CacheOpMsg_short_2::CACHE_OP_SHORT_2_MESSAGE_VERSION) {
    // Do remote link

    // Allocate memory for message header
    int flen = op_to_sizeof_fixedlen_msg(CACHE_LINK);
    int len = host_len;

    if ((flen + len) > DEFAULT_MAX_BUFFER_SIZE) // Bound marshalled data
      goto err_exit;

    d = new_IOBufferData(iobuffer_size_to_index(flen + len));
    msg = (char *) d->data();
    memcpy((msg + flen), hostname, host_len);

    // Setup args for remote link
    CacheOpArgs_Link linkArgs;
    linkArgs.from = from;
    linkArgs.to = to;
    linkArgs.frag_type = type;
    Action *action = CacheContinuation::do_op(cont, session, (void *) &linkArgs, CACHE_LINK, d, (flen + len));
    if (action)
      return action;
  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"CacheOpMsg_short_2 [CACHE_LINK] bad msg version");
    return 0;
  }

err_exit:
  cont->handleEvent(CACHE_EVENT_LINK_FAILED, NULL);
  return ACTION_RESULT_DONE;
}

inline Action *
Cluster_deref(ClusterMachine * m, Continuation * cont, CacheKey * key, CacheFragType type, char *hostname, int host_len)
{
  ClusterSession session;
  Ptr<IOBufferData> d = NULL;
  char *msg = NULL;

  if (cluster_create_session(&session, m, NULL, 0)) {
    cont->handleEvent(CACHE_EVENT_DEREF_FAILED, NULL);
    return ACTION_RESULT_DONE ;
  }

  int vers = CacheOpMsg_short::protoToVersion(m->msg_proto_major);
  if (vers == CacheOpMsg_short::CACHE_OP_SHORT_MESSAGE_VERSION) {
    // Do remote deref

    // Allocate memory for message header
    int flen = op_to_sizeof_fixedlen_msg(CACHE_DEREF);
    int len = host_len;

    if ((flen + len) > DEFAULT_MAX_BUFFER_SIZE) // Bound marshalled data
      goto err_exit;

    d = new_IOBufferData(iobuffer_size_to_index(flen + len));
    msg = (char *) d->data();
    memcpy((msg + flen), hostname, host_len);

    // Setup args for remote deref
    CacheOpArgs_Deref drefArgs;
    drefArgs.md5 = key;
    drefArgs.frag_type = type;
    Action *action = CacheContinuation::do_op(cont, session, (void *) &drefArgs, CACHE_DEREF, d, (flen + len));
    if (action)
      return action;
  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"CacheOpMsg_short [CACHE_DEREF] bad msg version");
    return 0;
  }

err_exit:
  cont->handleEvent(CACHE_EVENT_DEREF_FAILED, NULL);
  return ACTION_RESULT_DONE ;
}

inline Action *
Cluster_remove(ClusterMachine * m, Continuation * cont, CacheKey * key,
               bool rm_user_agents, bool rm_link, CacheFragType frag_type, char *hostname, int host_len)
{
  ClusterSession session;
  Ptr<IOBufferData> d = NULL;
  char *msg = NULL;

  if (cluster_create_session(&session, m, NULL, 0)) {
    if (cont)
      cont->handleEvent(CACHE_EVENT_REMOVE_FAILED, NULL);
    return ACTION_RESULT_DONE;
  }

  int vers = CacheOpMsg_short::protoToVersion(m->msg_proto_major);
  if (vers == CacheOpMsg_short::CACHE_OP_SHORT_MESSAGE_VERSION) {
    // Do remote update

    // Allocate memory for message header
    int flen = op_to_sizeof_fixedlen_msg(CACHE_REMOVE);
    int len = host_len;

    if ((flen + len) > DEFAULT_MAX_BUFFER_SIZE) // Bound marshalled data
      goto err_exit;

    d = new_IOBufferData(iobuffer_size_to_index(flen + len));
    msg = (char *) d->data();
    memcpy((msg + flen), hostname, host_len);

    // Setup args for remote update
    CacheOpArgs_General updateArgs;
    updateArgs.url_md5 = key;
    updateArgs.cfl_flags |= (rm_user_agents ? CFL_REMOVE_USER_AGENTS : 0);
    updateArgs.cfl_flags |= (rm_link ? CFL_REMOVE_LINK : 0);
    updateArgs.frag_type = frag_type;
    Action *action = CacheContinuation::do_op(cont, session, (void *) &updateArgs, CACHE_REMOVE, d, (flen + len));
    if (action)
      return action;
  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"CacheOpMsg_short [CACHE_REMOVE] bad msg version");
    return (Action *) 0;
  }

err_exit:
  if (cont)
    cont->handleEvent(CACHE_EVENT_REMOVE_FAILED, NULL);
  return ACTION_RESULT_DONE;
}

#endif /* __CLUSTERINLINE_H__ */
