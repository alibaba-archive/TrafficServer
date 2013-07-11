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

  ClusterCacheInternal.h
****************************************************************************/

#ifndef __P_CLUSTERCACHEINTERNAL_H__
#define __P_CLUSTERCACHEINTERNAL_H__
#include "P_ClusterCache.h"
#include "I_OneWayTunnel.h"

//
// Compilation Options
//
#define CACHE_USE_OPEN_VIO              0       // EXPERIMENTAL: not fully tested
#define DO_REPLICATION                  0       // EXPERIMENTAL: not fully tested

//
// Constants
//
#define META_DATA_FAST_ALLOC_LIMIT      1
#define CACHE_CLUSTER_TIMEOUT           HRTIME_MSECONDS(5000)
#define CACHE_RETRY_PERIOD              HRTIME_MSECONDS(10)
#define REMOTE_CONNECT_HASH             (16 * 1024)

#if DEBUG
extern int64_t num_of_cachecontinuation;
#endif
//
// Macros
//
#define FOLDHASH(_ip,_seq) (_seq % REMOTE_CONNECT_HASH)
#define ALIGN_DOUBLE(_p)   ((((uintptr_t) (_p)) + 7) & ~7)
#define ALLOCA_DOUBLE(_sz) ALIGN_DOUBLE(alloca((_sz) + 8))


//
// Testing
//
#define TEST(_x)
//#define TEST(_x) _x

//#define TTEST(_x)
//fprintf(stderr, _x " at: %d\n",
//      ((unsigned int)(ink_get_hrtime()/HRTIME_MSECOND)) % 1000)
#define TTEST(_x)

//#define TIMEOUT_TEST(_x) _x
#define TIMEOUT_TEST(_x)

extern int cache_migrate_on_demand;
extern int ET_CLUSTER;
//
// Compile time options.
//
// Only one of PROBE_LOCAL_CACHE_FIRST or PROBE_LOCAL_CACHE_LAST
// should be set.  These indicate that the local cache should be
// probed at this point regardless of the dedicated location of the
// object.  Note, if the owning machine goes down the local machine
// will be probed anyway.
//
#define PROBE_LOCAL_CACHE_FIRST        DO_REPLICATION
#define PROBE_LOCAL_CACHE_LAST         false

//
// This continuation handles all cache cluster traffic, on both
// sides (state machine client and cache server)
//

//struct CacheSM: public Continuation
//{
//  Ptr<ProxyMutex> lmutex;
//  // msg list
//  ClusterSession *scs;
//  CacheVConnection *cache_vc;
//  CacheFragType frag_type;
//  int request_opcode;
//  bool request_purge;
//  bool use_deferred_callback;
//  time_t pin_in_cache;
//  bool have_all_data;
//  bool zero_body;
//  unsigned int seq_number;
//
//  INK_MD5 url_md5;
//  CacheHTTPHdr ic_request;
//  CacheHTTPHdr ic_response;
//  CacheLookupHttpConfig *ic_params;
//
//  Action *cache_action;
//  Ptr<IOBufferData> ic_hostname;
//  int ic_hostname_len;
//  ink_hrtime start_time;
//
//  int result;                   // return event code
//  int result_error;             // error code associated with event
//};
//
//CacheSM * new_CacheSM();
//void free_CacheSM(CacheSM * cache_sm);
//
//extern ClassAllocator<CacheSM> cacheSMAllocator;

struct ClusterCont: public Continuation
{
  ClusterSession session;
  Ptr<IOBufferBlock> data;
  void *context;
  int func_id;
  int data_len;

  Action _action;
  int handleEvent(int event, void *d);
  IOBufferData *copy_data();
  int copy_data(char *buf, int size);
  void consume(int size);
};

inline IOBufferData *
ClusterCont::copy_data() {
  IOBufferData *buf = new_IOBufferData(iobuffer_size_to_index(data_len, MAX_BUFFER_SIZE_INDEX));
  char *p = buf->data();
  for (IOBufferBlock *b = data; b; b = b->next) {
    memcpy(p, b->_start, b->_end - b->_start);
    p += b->_end - b->_start;
  }
  return buf;
}

inline void
ClusterCont::consume(int size) {

  int64_t sz = size;
  while (data && sz >= data->read_avail()) {
    sz -= data->read_avail();
    data = data->next;
  }
  if (data)
    data->_start += sz;

  data_len = data_len > size ? (data_len - size) : 0;
}

inline int
ClusterCont::copy_data(char *buf, int len)
{
  ink_debug_assert(data_len >= len);
  IOBufferBlock *b = data;
  int64_t sz = len;
  while (len > 0 && b) {
    int64_t avail = b->read_avail();
    sz -= avail;
    if (sz < 0) {
      memcpy(buf, b->_start, avail + sz);
      sz = 0;
      break;
    } else {
      memcpy(buf, b->_start, avail);
      buf += avail;
      b = b->next;
    }
  }
  return len - (int) sz;
}
extern ClassAllocator<ClusterCont> clusterContAllocator;

inline int
ClusterCont::handleEvent(int event, void *d) {
  if (func_id == CLUSTER_CACHE_OP_CLUSTER_FUNCTION)
    cache_op_ClusterFunction(session, context, this);
  else if (func_id == CLUSTER_CACHE_OP_RESULT_CLUSTER_FUNCTION)
    cache_op_result_ClusterFunction(session, context, this);
  else
    _action.continuation->handleEvent(func_id, this);

  mutex.clear();
  _action.mutex.clear();
  data = NULL;

  clusterContAllocator.free(this);
  return EVENT_DONE;
}

struct CacheContinuation;
typedef int (CacheContinuation::*CacheContHandler) (int, void *);
struct CacheContinuation:public Continuation
{
  static int size_to_init;
  enum
  {
    MagicNo = 0x92183123
  };
  int magicno;
  INK_MD5 url_md5;

  ClusterMachine *past_probes[CONFIGURATION_HISTORY_PROBE_DEPTH];

  ClusterVCToken token;

  CacheHTTPInfo cache_vc_info; // for get_http_info
//  MIOBuffer doc_data;
  Ptr<IOBufferBlock> doc_data;
  // Incoming data generated from unmarshaling request/response ops
  Ptr<IOBufferData> rw_buf_msg;
  Arena ic_arena;
  CacheHTTPHdr ic_request; // for lookup or read
  CacheHTTPInfo ic_old_info; // for update
  CacheHTTPInfo ic_new_info; // for set_http_info

  ClusterSession cs;
  char *ic_hostname;
  int ic_hostname_len;

  ink_hrtime start_time;
  ClusterMachine *target_machine;
  int probe_depth;

  CacheVC *cache_vc;
  Action *pending_action;
  bool cache_read;
  bool request_purge;
  bool have_all_data;           // all object data in response
  bool expect_next;
  bool writer_aborted;
  int result;                   // return event code
  int result_error;             // error code associated with event
  uint16_t cfl_flags;             // Request flags; see CFL_XXX defines

  unsigned int seq_number;
  CacheFragType frag_type;
  int nbytes;       // the msg nbyts
  unsigned int target_ip;
  int request_opcode;
  int header_len;
  int rw_buf_msg_len;

  time_t pin_in_cache;
  int64_t doc_size;
  int64_t total_length;
  VIO *vio;                  //
  IOBufferReader *reader;    // for normal read
  CacheLookupHttpConfig *ic_params;
  MIOBuffer *mbuf;
  EThread *thread;

//  int lookupEvent(int event, void *d);
//  int probeLookupEvent(int event, void *d);
//  int remoteOpEvent(int event, Event * e);
//  int replyLookupEvent(int event, void *d);
  int replyOpEvent();
//  int handleReplyEvent(int event, Event * e);
//  int callbackEvent(int event, Event * e);
  int setupVCdataRead(int event, void *data);
  int setupVCdataWrite(int event, void *data);
  int setupVCdataRemove(int event, void *data);
  int setupVCdataLink(int event, void *data);
  int setupVCdataDeref(int event, void *data);
  int VCdataRead(int event, void *data);
  int VCdataWrite(int event, void *data);
  int VCSmallDataRead(int event, void *data);
//  int setupReadWriteVC(int, VConnection *);
//  ClusterVConnection *lookupOpenWriteVC();
//  int lookupOpenWriteVCEvent(int, Event *);
//  int localVCsetupEvent(int event, ClusterVConnection * vc);
//  void insert_cache_callback_user(ClusterVConnection *, int, void *);
//  int insertCallbackEvent(int, Event *);
//  void callback_user(int result, void *d);
//  void defer_callback_result(int result, void *d);
//  int callbackResultEvent(int event, Event * e);
//  void setupReadBufTunnel(VConnection *, VConnection *);
//  int tunnelClosedEvent(int event, void *);
//  int remove_and_delete(int, Event *);


  inline void setMsgBufferLen(int l, IOBufferData * b = 0) {
    ink_assert(rw_buf_msg == 0);
    ink_assert(rw_buf_msg_len == 0);

    rw_buf_msg = b;
    rw_buf_msg_len = l;
  }

  inline int getMsgBufferLen()
  {
    return rw_buf_msg_len;
  }

  inline void allocMsgBuffer()
  {
    ink_assert(rw_buf_msg == 0);
    ink_assert(rw_buf_msg_len);
    if (rw_buf_msg_len <= DEFAULT_MAX_BUFFER_SIZE) {
      rw_buf_msg = new_IOBufferData(buffer_size_to_index(rw_buf_msg_len, MAX_BUFFER_SIZE_INDEX));
    } else {
      rw_buf_msg = new_xmalloc_IOBufferData(ats_malloc(rw_buf_msg_len), rw_buf_msg_len);
    }
  }

  inline char *getMsgBuffer()
  {
    ink_assert(rw_buf_msg);
    return rw_buf_msg->data();
  }

  inline IOBufferData *getMsgBufferIOBData()
  {
    return rw_buf_msg;
  }

  inline void freeMsgBuffer()
  {
    if (rw_buf_msg) {
      rw_buf_msg = 0;
      rw_buf_msg_len = 0;
    }
  }

  inline void free()
  {
    token.clear();

    if (cache_vc_info.valid()) {
      cache_vc_info.destroy();
    }
    // Deallocate unmarshaled data
    if (ic_params) {
      delete ic_params;
      ic_params = 0;
    }
    if (ic_request.valid()) {
      ic_request.clear();
    }
//    if (ic_response.valid()) {
//      ic_response.clear();
//    }
    if (ic_old_info.valid()) {
      ic_old_info.destroy();
    }
    if (ic_new_info.valid()) {
      ic_new_info.destroy();
    }
//    ic_arena.reset();
    freeMsgBuffer();
//
//    tunnel_mutex = 0;
//    readahead_data = 0;
    ic_hostname = 0;
  }

  CacheContinuation(): magicno(MagicNo) {
    size_to_init = sizeof(CacheContinuation) - (size_t) & ((CacheContinuation *) 0)->cs;
    memset((char *) &cs, 0, size_to_init);
  }

  inline static bool is_ClusterThread(EThread * et)
  {
    int etype = ET_CLUSTER;
    int i;
    for (i = 0; i < eventProcessor.n_threads_for_type[etype]; ++i) {
      if (et == eventProcessor.eventthread[etype][i]) {
        return true;
      }
    }
    return false;
  }

  // Static class member functions
  static int init();
  static CacheContinuation *cacheContAllocator_alloc();
  static void cacheContAllocator_free(CacheContinuation *);
  inkcoreapi static Action *callback_failure(Action *, int, int, CacheContinuation * this_cc = 0);
  static Action *do_remote_lookup(Continuation *, CacheKey *, CacheContinuation *, CacheFragType, char *, int);
  inkcoreapi static Action *do_op(Continuation * c, ClusterSession cs, void *args,
                           int user_opcode, IOBufferData *data, int data_len, int nbytes = -1, MIOBuffer * b = 0);
  static int setup_local_vc(char *data, int data_len, CacheContinuation * cc, ClusterMachine * mp, Action **);
  static void disposeOfDataBuffer(void *buf);
  static int handleDisposeEvent(int event, CacheContinuation * cc);
  int32_t getObjectSize(VConnection *, int, CacheHTTPInfo *);
};

extern ClassAllocator<CacheContinuation> cacheContAllocator;

inline CacheContinuation *
new_CacheCont(EThread *t) {
#ifdef DEBUG
  ink_atomic_increment(&num_of_cachecontinuation, 1);
#endif
  ink_assert(t == this_ethread());
  CacheContinuation *c = THREAD_ALLOC(cacheContAllocator, t);
  c->mutex = new_ProxyMutex();
  c->start_time = ink_get_hrtime();
  c->thread = t;
  return c;
}

inline void
free_CacheCont(CacheContinuation *c) {
#ifdef DEBUG
  ink_atomic_increment(&num_of_cachecontinuation, -1);
#endif
  ink_assert(c->magicno == (int) c->MagicNo && !c->expect_next);
//  ink_assert(!c->cache_op_ClusterFunction);
  if (c->pending_action) {
    c->pending_action->cancel();
    c->pending_action = NULL;
  }
  if (c->cache_vc) {
    c->cache_vc->do_io(VIO::CLOSE);
    c->cache_vc = NULL;
  }
  if (c->mbuf) {
    free_MIOBuffer(c->mbuf);
    c->mbuf = NULL;
  }

  c->magicno = -1;
  c->token.clear();
  c->cache_vc_info.clear();
  if (c->ic_params) {
    delete c->ic_params;
    c->ic_params = 0;
  }
  c->ic_request.clear();
  c->ic_old_info.clear();
  c->ic_new_info.destroy();
  c->ic_arena.reset();
  c->freeMsgBuffer();
  c->ic_hostname = 0;
  c->mutex.clear();

  c->doc_data = NULL;

  cacheContAllocator.free(c);
}
/////////////////////////////////////////
// Cache OP specific args for do_op()  //
/////////////////////////////////////////

// Bit definitions for cfl_flags.
// Note: Limited to 16 bits
#define CFL_OVERWRITE_ON_WRITE 		(1 << 1)
#define CFL_REMOVE_USER_AGENTS 		(1 << 2)
#define CFL_REMOVE_LINK 		(1 << 3)
#define CFL_LOPENWRITE_HAVE_OLDINFO	(1 << 4)
#define CFL_ALLOW_MULTIPLE_WRITES       (1 << 5)
#define CFL_MAX 			(1 << 15)

struct CacheOpArgs_General
{
  INK_MD5 *url_md5;
  time_t pin_in_cache;          // open_write() specific arg
  CacheFragType frag_type;
  uint16_t cfl_flags;

    CacheOpArgs_General():url_md5(NULL), pin_in_cache(0), frag_type(CACHE_FRAG_TYPE_NONE), cfl_flags(0)
  {
  }
};

struct CacheOpArgs_Link
{
  INK_MD5 *from;
  INK_MD5 *to;
  uint16_t cfl_flags;             // see CFL_XXX defines
  CacheFragType frag_type;

    CacheOpArgs_Link():from(NULL), to(NULL), cfl_flags(0), frag_type(CACHE_FRAG_TYPE_NONE)
  {
  }
};

struct CacheOpArgs_Deref
{
  INK_MD5 *md5;
  uint16_t cfl_flags;             // see CFL_XXX defines
  CacheFragType frag_type;

    CacheOpArgs_Deref():md5(NULL), cfl_flags(0), frag_type(CACHE_FRAG_TYPE_NONE)
  {
  }
};

///////////////////////////////////
// Over the wire message formats //
///////////////////////////////////
struct CacheLookupMsg:public ClusterMessageHeader
{
  INK_MD5 url_md5;
  uint32_t seq_number;
  uint32_t frag_type;
  uint8_t moi[4];
  enum
  {
    MIN_VERSION = 1,
    MAX_VERSION = 1,
    CACHE_LOOKUP_MESSAGE_VERSION = MAX_VERSION
  };
  CacheLookupMsg(uint16_t vers = CACHE_LOOKUP_MESSAGE_VERSION):
  ClusterMessageHeader(vers), seq_number(0), frag_type(0) {
    memset(moi, 0, sizeof(moi));
  }

  //////////////////////////////////////////////////////////////////////////
  static int protoToVersion(int protoMajor)
  {
    (void) protoMajor;
    return CACHE_LOOKUP_MESSAGE_VERSION;
  }
  static int sizeof_fixedlen_msg()
  {
    CacheLookupMsg *p = 0;

    // Maybe use offsetoff here instead. /leif
    return (int) ALIGN_DOUBLE(&p->moi[0]);
  }
  void init(uint16_t vers = CACHE_LOOKUP_MESSAGE_VERSION) {
    _init(vers);
  }
  inline void SwapBytes()
  {
    if (NeedByteSwap()) {
      ink_release_assert(!"No byte swap for INK_MD5");
      ats_swap32(&seq_number);
      ats_swap32(&frag_type);
    }
  }
  //////////////////////////////////////////////////////////////////////////
};

struct CacheOpMsg_long:public ClusterMessageHeader
{
  uint8_t opcode;
  uint8_t frag_type;
  uint16_t cfl_flags;             // see CFL_XXX defines
  INK_MD5 url_md5;
  uint32_t seq_number;
  uint32_t nbytes;
  uint32_t data;                  // used by open_write()
  int32_t channel;                // used by open interfaces
  ClusterVCToken token;
  int32_t buffer_size;            // used by open read interface
  uint8_t moi[4];
  enum
  {
    MIN_VERSION = 1,
    MAX_VERSION = 1,
    CACHE_OP_LONG_MESSAGE_VERSION = MAX_VERSION
  };
  CacheOpMsg_long(uint16_t vers = CACHE_OP_LONG_MESSAGE_VERSION):
  ClusterMessageHeader(vers),
    opcode(0), frag_type(0), cfl_flags(0), seq_number(0), nbytes(0), data(0), channel(0), buffer_size(0) {
    memset(moi, 0, sizeof(moi));
  }

  //////////////////////////////////////////////////////////////////////////
  static int protoToVersion(int protoMajor)
  {
    (void) protoMajor;
    return CACHE_OP_LONG_MESSAGE_VERSION;
  }
  static int sizeof_fixedlen_msg()
  {
    CacheOpMsg_long *p = 0;

    // Change to offsetof maybe? /leif
    return (int) ALIGN_DOUBLE(&p->moi[0]);
  }
  void init(uint16_t vers = CACHE_OP_LONG_MESSAGE_VERSION) {
    _init(vers);
  }
  inline void SwapBytes()
  {
    if (NeedByteSwap()) {
      ink_release_assert(!"No byte swap for INK_MD5");
      ats_swap16(&cfl_flags);
      ats_swap32(&seq_number);
      ats_swap32(&nbytes);
      ats_swap32(&data);
      ats_swap32((uint32_t *) & channel);
      token.SwapBytes();
      ats_swap32((uint32_t *) & buffer_size);
      ats_swap32((uint32_t *) & frag_type);
    }
  }
  //////////////////////////////////////////////////////////////////////////
};

struct CacheOpMsg_short:public ClusterMessageHeader
{
  uint8_t opcode;
  uint8_t frag_type;              // currently used by open_write() (low level)
  uint16_t cfl_flags;             // see CFL_XXX defines
  INK_MD5 md5;
  uint32_t seq_number;
  uint32_t nbytes;
  uint32_t data;                  // currently used by open_write() (low level)
  int32_t channel;                // used by open interfaces
  ClusterVCToken token;         // used by open interfaces
  int32_t buffer_size;            // used by open read interface

  // Variable portion of message
  uint8_t moi[4];
  enum
  {
    MIN_VERSION = 1,
    MAX_VERSION = 1,
    CACHE_OP_SHORT_MESSAGE_VERSION = MAX_VERSION
  };
  CacheOpMsg_short(uint16_t vers = CACHE_OP_SHORT_MESSAGE_VERSION):
  ClusterMessageHeader(vers),
    opcode(0), frag_type(0), cfl_flags(0), seq_number(0), nbytes(0), data(0), channel(0), buffer_size(0) {
    memset(moi, 0, sizeof(moi));
  }

  //////////////////////////////////////////////////////////////////////////
  static int protoToVersion(int protoMajor)
  {
    (void) protoMajor;
    return CACHE_OP_SHORT_MESSAGE_VERSION;
  }
  static int sizeof_fixedlen_msg()
  {
    CacheOpMsg_short *p = 0;
    // Use offsetof. /leif
    return (int) ALIGN_DOUBLE(&p->moi[0]);
  }
  void init(uint16_t vers = CACHE_OP_SHORT_MESSAGE_VERSION) {
    _init(vers);
  }
  inline void SwapBytes()
  {
    if (NeedByteSwap()) {
      ink_release_assert(!"No byte swap for INK_MD5");
      ats_swap16(&cfl_flags);
      ats_swap32(&seq_number);
      ats_swap32(&nbytes);
      ats_swap32(&data);
      if (opcode == CACHE_OPEN_READ) {
        ats_swap32((uint32_t *) & buffer_size);
        ats_swap32((uint32_t *) & channel);
        token.SwapBytes();
      }
    }
  }
  //////////////////////////////////////////////////////////////////////////
};

struct CacheOpMsg_short_2:public ClusterMessageHeader
{
  uint8_t opcode;
  uint8_t frag_type;
  uint16_t cfl_flags;             // see CFL_XXX defines
  INK_MD5 md5_1;
  INK_MD5 md5_2;
  uint32_t seq_number;
  uint8_t moi[4];
  enum
  {
    MIN_VERSION = 1,
    MAX_VERSION = 1,
    CACHE_OP_SHORT_2_MESSAGE_VERSION = MAX_VERSION
  };
    CacheOpMsg_short_2(uint16_t vers = CACHE_OP_SHORT_2_MESSAGE_VERSION)
:  ClusterMessageHeader(vers), opcode(0), frag_type(0), cfl_flags(0), seq_number(0) {
    memset(moi, 0, sizeof(moi));
  }
  //////////////////////////////////////////////////////////////////////////
  static int protoToVersion(int protoMajor)
  {
    (void) protoMajor;
    return CACHE_OP_SHORT_2_MESSAGE_VERSION;
  }
  static int sizeof_fixedlen_msg()
  {
    CacheOpMsg_short_2 *p = 0;
    // Use offsetof already. /leif
    return (int) ALIGN_DOUBLE(&p->moi[0]);
  }
  void init(uint16_t vers = CACHE_OP_SHORT_2_MESSAGE_VERSION) {
    _init(vers);
  }
  inline void SwapBytes()
  {
    if (NeedByteSwap()) {
      ink_release_assert(!"No byte swap for MD5_1");
      ink_release_assert(!"No byte swap for MD5_2");
      ats_swap16(&cfl_flags);
      ats_swap32(&seq_number);
    }
  }
  //////////////////////////////////////////////////////////////////////////
};

struct CacheOpReplyMsg:public ClusterMessageHeader
{
  uint32_t seq_number;
  int32_t result;
  int32_t h_len;
  int32_t d_len;
  int32_t reason; // // Used by CACHE_OPEN_READ & CACHE_LINK reply
  int64_t doc_size;

  enum
  {
    MIN_VERSION = 1,
    MAX_VERSION = 1,
    CACHE_OP_REPLY_MESSAGE_VERSION = MAX_VERSION
  };
  CacheOpReplyMsg(uint16_t vers = CACHE_OP_REPLY_MESSAGE_VERSION):
  ClusterMessageHeader(vers), seq_number(0), result(0), h_len(0), d_len(0), reason(0), doc_size(0) {
  }

  //////////////////////////////////////////////////////////////////////////
  static int protoToVersion(int protoMajor)
  {
    (void) protoMajor;
    return CACHE_OP_REPLY_MESSAGE_VERSION;
  }
  static int sizeof_fixedlen_msg()
  {
    return INK_ALIGN(sizeof (CacheOpReplyMsg), 16);
  }
  void init(uint16_t vers = CACHE_OP_REPLY_MESSAGE_VERSION) {
    _init(vers);
  }
  inline void SwapBytes()
  {
    if (NeedByteSwap()) {
      ats_swap32(&seq_number);
      ats_swap32((uint32_t *) & result);
      ats_swap32((uint32_t *) & reason);
      ats_swap64((uint64_t *) & doc_size);
    }
  }
  //////////////////////////////////////////////////////////////////////////
};

inline int
maxval(int a, int b)
{
  return ((a > b) ? a : b);
}

inline int
op_to_sizeof_fixedlen_msg(int op)
{
  switch (op) {
  case CACHE_LOOKUP_OP:
    {
      return CacheLookupMsg::sizeof_fixedlen_msg();
    }
  case CACHE_OPEN_WRITE_BUFFER:
  case CACHE_OPEN_WRITE_BUFFER_LONG:
    {
      ink_release_assert(!"op_to_sizeof_fixedlen_msg() op not supported");
      return 0;
    }
  case CACHE_OPEN_WRITE:
  case CACHE_OPEN_READ:
  case CACHE_OPEN_READ_BUFFER:
    {
      return CacheOpMsg_short::sizeof_fixedlen_msg();
    }
  case CACHE_OPEN_READ_LONG:
  case CACHE_OPEN_READ_BUFFER_LONG:
  case CACHE_OPEN_WRITE_LONG:
    {
      return CacheOpMsg_long::sizeof_fixedlen_msg();
    }
  case CACHE_UPDATE:
  case CACHE_REMOVE:
  case CACHE_DEREF:
    {
      return CacheOpMsg_short::sizeof_fixedlen_msg();
    }
  case CACHE_LINK:
    {
      return CacheOpMsg_short_2::sizeof_fixedlen_msg();
    }
  default:
    {
      ink_release_assert(!"op_to_sizeof_fixedlen_msg() unknown op");
      return 0;
    }
  }                             // End of switch
}

//////////////////////////////////////////////////////////////////////////////

static inline bool
event_is_lookup(int event)
{
  switch (event) {
  default:
    return false;
  case CACHE_EVENT_LOOKUP:
  case CACHE_EVENT_LOOKUP_FAILED:
    return true;
  }
}

static inline bool
event_is_open(int event)
{
  switch (event) {
  default:
    return false;
  case CACHE_EVENT_OPEN_READ:
  case CACHE_EVENT_OPEN_WRITE:
    return true;
  }
}

static inline bool
op_is_read(int opcode)
{
  switch (opcode) {
  case CACHE_OPEN_READ:
  case CACHE_OPEN_READ_LONG:
  case CACHE_OPEN_READ_BUFFER:
  case CACHE_OPEN_READ_BUFFER_LONG:
    return true;
  default:
    return false;
  }
}

static inline bool
op_is_shortform(int opcode)
{
  switch (opcode) {
  case CACHE_OPEN_READ:
  case CACHE_OPEN_READ_BUFFER:
  case CACHE_OPEN_WRITE:
  case CACHE_OPEN_WRITE_BUFFER:
    return true;
  default:
    return false;
  }
}

static inline int
op_failure(int opcode)
{
  switch (opcode) {
  case CACHE_OPEN_WRITE:
  case CACHE_OPEN_WRITE_LONG:
  case CACHE_OPEN_WRITE_BUFFER:
  case CACHE_OPEN_WRITE_BUFFER_LONG:
    return CACHE_EVENT_OPEN_WRITE_FAILED;

  case CACHE_OPEN_READ:
  case CACHE_OPEN_READ_LONG:
  case CACHE_OPEN_READ_BUFFER:
  case CACHE_OPEN_READ_BUFFER_LONG:
    return CACHE_EVENT_OPEN_READ_FAILED;

  case CACHE_UPDATE:
    return CACHE_EVENT_UPDATE_FAILED;
  case CACHE_REMOVE:
    return CACHE_EVENT_REMOVE_FAILED;
  case CACHE_LINK:
    return CACHE_EVENT_LINK_FAILED;
  case CACHE_DEREF:
    return CACHE_EVENT_DEREF_FAILED;
  }
  return -1;
}

static inline int
op_needs_marshalled_coi(int opcode)
{
  switch (opcode) {
  case CACHE_OPEN_WRITE:
  case CACHE_OPEN_WRITE_BUFFER:
  case CACHE_OPEN_READ:
  case CACHE_OPEN_READ_BUFFER:
  case CACHE_REMOVE:
  case CACHE_LINK:
  case CACHE_DEREF:
    return 0;

  case CACHE_OPEN_WRITE_LONG:
  case CACHE_OPEN_WRITE_BUFFER_LONG:
  case CACHE_OPEN_READ_LONG:
  case CACHE_OPEN_READ_BUFFER_LONG:
  case CACHE_UPDATE:
    return 0;

  default:
    return 0;
  }
}

static inline int
event_reply_may_have_moi(int event)
{
  switch (event) {
  case CACHE_EVENT_OPEN_READ:
  case CACHE_EVENT_OPEN_WRITE:
  case CACHE_EVENT_LINK:
  case CACHE_EVENT_LINK_FAILED:
  case CACHE_EVENT_OPEN_READ_FAILED:
  case CACHE_EVENT_OPEN_WRITE_FAILED:
  case CACHE_EVENT_REMOVE_FAILED:
  case CACHE_EVENT_UPDATE_FAILED:
  case CACHE_EVENT_DEREF_FAILED:
    return true;
  default:
    return false;
  }
}

static inline int
event_is_failure(int event)
{
  switch (event) {
  case CACHE_EVENT_LOOKUP_FAILED:
  case CACHE_EVENT_OPEN_READ_FAILED:
  case CACHE_EVENT_OPEN_WRITE_FAILED:
  case CACHE_EVENT_UPDATE_FAILED:
  case CACHE_EVENT_REMOVE_FAILED:
  case CACHE_EVENT_LINK_FAILED:
  case CACHE_EVENT_DEREF_FAILED:
    return true;
  default:
    return false;
  }
}

#endif // __CLUSTERCACHEINTERNAL_H__
