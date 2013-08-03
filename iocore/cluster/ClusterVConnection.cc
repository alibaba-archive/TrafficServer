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

  ClusterVConnection.cc
****************************************************************************/

#include "P_Cluster.h"
ClassAllocator<ClusterVConnection> clusterVCAllocator("clusterVCAllocator");
ClassAllocator<ByteBankDescriptor> byteBankAllocator("byteBankAllocator");
ClassAllocator<ClusterCacheVC> clusterCacheVCAllocator("custerCacheVCAllocator");

int ClusterCacheVC::size_to_init = -1;

#define CLUSTER_WRITE_MIN_SIZE (1 << 14)

#define CLUSTER_CACHE_VC_CLOSE_SESSION \
{ \
  cluster_close_session(cs); \
  session_closed = true; \
}

ByteBankDescriptor *
ByteBankDescriptor::ByteBankDescriptor_alloc(IOBufferBlock * iob)
{
  ByteBankDescriptor *b = byteBankAllocator.alloc();
  b->block = iob;
  return b;
}

void
ByteBankDescriptor::ByteBankDescriptor_free(ByteBankDescriptor * b)
{
  b->block = 0;
  byteBankAllocator.free(b);
}

void
clusterVCAllocator_free(ClusterVConnection * vc)
{
  vc->mutex = 0;
  vc->action_ = 0;
  vc->free();

  if (VC_CLUSTER_WRITE_SCHEDULE == vc->type) {
    vc->type = VC_CLUSTER_CLOSED;
    return;
  }
  clusterVCAllocator.free(vc);
}

ClusterVConnState::ClusterVConnState():enabled(0), priority(1), vio(VIO::NONE), queue(0), ifd(-1), delay_timeout(NULL)
{
}

ClusterVConnectionBase::ClusterVConnectionBase():
thread(0), closed(0), inactivity_timeout_in(0), active_timeout_in(0), inactivity_timeout(NULL), active_timeout(NULL)
{
}

#ifdef DEBUG
int
  ClusterVConnectionBase::enable_debug_trace = 0;
#endif

VIO *
ClusterVConnectionBase::do_io_read(Continuation * acont, int64_t anbytes, MIOBuffer * abuffer)
{
  ink_assert(!closed);
  read.vio.buffer.writer_for(abuffer);
  read.vio.op = VIO::READ;
  read.vio.set_continuation(acont);
  read.vio.nbytes = anbytes;
  read.vio.ndone = 0;
  read.vio.vc_server = (VConnection *) this;
  read.enabled = 1;

  ClusterVConnection *cvc = (ClusterVConnection *) this;
  Debug("cluster_vc_xfer", "do_io_read [%s] chan %d", "", cvc->channel);
  return &read.vio;
}

VIO *
ClusterVConnectionBase::do_io_pread(Continuation * acont, int64_t anbytes, MIOBuffer * abuffer, int64_t off)
{
  NOWARN_UNUSED(acont);
  NOWARN_UNUSED(anbytes);
  NOWARN_UNUSED(abuffer);
  NOWARN_UNUSED(off);
  ink_assert(!"implemented");
  return 0;
}

int
ClusterVConnection::get_header(void **ptr, int *len)
{
  NOWARN_UNUSED(ptr);
  NOWARN_UNUSED(len);
  ink_assert(!"implemented");
  return -1;
}

int
ClusterVConnection::set_header(void *ptr, int len)
{
  NOWARN_UNUSED(ptr);
  NOWARN_UNUSED(len);
  ink_assert(!"implemented");
  return -1;
}

int
ClusterVConnection::get_single_data(void **ptr, int *len)
{
  NOWARN_UNUSED(ptr);
  NOWARN_UNUSED(len);
  ink_assert(!"implemented");
  return -1;
}

VIO *
ClusterVConnectionBase::do_io_write(Continuation * acont, int64_t anbytes, IOBufferReader * abuffer, bool owner)
{
  ink_assert(!closed);
  ink_assert(!owner);
  write.vio.buffer.reader_for(abuffer);
  write.vio.op = VIO::WRITE;
  write.vio.set_continuation(acont);
  write.vio.nbytes = anbytes;
  write.vio.ndone = 0;
  write.vio.vc_server = (VConnection *) this;
  write.enabled = 1;

  return &write.vio;
}

void
ClusterVConnectionBase::do_io_close(int alerrno)
{
  read.enabled = 0;
  write.enabled = 0;
  read.vio.buffer.clear();
  write.vio.buffer.clear();
  INK_WRITE_MEMORY_BARRIER;
  if (alerrno && alerrno != -1)
    this->lerrno = alerrno;

  if (alerrno == -1) {
    closed = 1;
  } else {
    closed = -1;
  }
}

void
ClusterVConnectionBase::reenable(VIO * vio)
{
  ink_assert(!closed);
  if (vio == &read.vio) {
    read.enabled = 1;
#ifdef DEBUG
    if (enable_debug_trace && (vio->buffer.mbuf && !vio->buffer.writer()->write_avail()))
      printf("NetVConnection re-enabled for read when full\n");
#endif
  } else if (vio == &write.vio) {
    write.enabled = 1;
#ifdef DEBUG
    if (enable_debug_trace && (vio->buffer.mbuf && !vio->buffer.reader()->read_avail()))
      printf("NetVConnection re-enabled for write when empty\n");
#endif
  } else {
    ink_assert(!"bad vio");
  }
}

void
ClusterVConnectionBase::reenable_re(VIO * vio)
{
  reenable(vio);
}

ClusterVConnection::ClusterVConnection(int is_new_connect_read)
  :  ch(NULL),
     new_connect_read(is_new_connect_read),
     remote_free(0),
     last_local_free(0),
     channel(0),
     close_disabled(0),
     remote_closed(0),
     remote_close_disabled(1),
     remote_lerrno(0),
     start_time(0),
     last_activity_time(0),
     n_set_data_msgs(0),
     n_recv_set_data_msgs(0),
     pending_remote_fill(0),
     have_all_data(0),
     initial_data_bytes(0),
     current_cont(0),
     iov_map(CLUSTER_IOV_NOT_OPEN),
     write_list_tail(0),
     write_list_bytes(0),
     write_bytes_in_transit(0),
     alternate(),
     time_pin(0),
     disk_io_priority(0)
{
#ifdef DEBUG
  read.vio.buffer.name = "ClusterVConnection.read";
  write.vio.buffer.name = "ClusterVConnection.write";
#endif
  SET_HANDLER((ClusterVConnHandler) & ClusterVConnection::startEvent);
}

ClusterVConnection::~ClusterVConnection()
{
  free();
}

void
ClusterVConnection::free()
{
  if (alternate.valid()) {
    alternate.destroy();
  }
  ByteBankDescriptor *d;
  while ((d = byte_bank_q.dequeue())) {
    ByteBankDescriptor::ByteBankDescriptor_free(d);
  }
  read_block = 0;
  remote_write_block = 0;
  marshal_buf = 0;
  write_list = 0;
  write_list_tail = 0;
  write_list_bytes = 0;
  write_bytes_in_transit = 0;
}

void
ClusterVConnection::do_io_close(int alerrno)
{
#ifdef CLUSTER_TOMCAT
  ProxyMutex *mutex = (this_ethread())->mutex;
#endif
  ink_hrtime now = ink_get_hrtime();
  CLUSTER_SUM_DYN_STAT(CLUSTER_CON_TOTAL_TIME_STAT, now - start_time);
  CLUSTER_SUM_DYN_STAT(CLUSTER_LOCAL_CONNECTION_TIME_STAT, now - start_time);
  ClusterVConnectionBase::do_io_close(alerrno);
}

int
ClusterVConnection::startEvent(int event, Event * e)
{
  //
  // Safe to call with e == NULL from the same thread.
  //
  (void) event;
  start(e ? e->ethread : (EThread *) NULL);
  return EVENT_DONE;
}

int
ClusterVConnection::mainEvent(int event, Event * e)
{
  (void) event;
  (void) e;
  ink_assert(!"unexpected event");
  return EVENT_DONE;
}

int
ClusterVConnection::start(EThread * t)
{
  //
  //  New channel connect protocol.  Establish VC locally and send the
  //  channel id to the target.  Reverse of existing connect protocol
  //
  //////////////////////////////////////////////////////////////////////////
  // In the new VC connect protocol, we always establish the local side
  // of the connection followed by the remote side.
  //
  // Read connection notes:
  // ----------------------
  // The response message now consists of the standard reply message
  // along with a portion of the  object data.  This data is always
  // transferred in the same Cluster transfer message as channel data.
  // In order to transfer data into a partially connected VC, we introduced
  // a VC "pending_remote_fill" state allowing us to move the initial data
  // using the existing user channel mechanism.
  // Initially, both sides of the connection set "pending_remote_fill".
  //
  // "pending_remote_fill" allows us to make the following assumptions.
  //   1) No free space messages are sent for VC(s) in this state.
  //   2) Writer side, the initial write data is described by
  //      vc->remote_write_block NOT by vc->write.vio.buffer, since
  //      vc->write.vio is reserved for use in the OneWayTunnel.
  //      OneWayTunnel is used when all the object data cannot be
  //      contained in the initial send buffer.
  //   3) Writer side, write vio mutex not acquired for initial data write.
  ///////////////////////////////////////////////////////////////////////////

  int status;
  if (!channel) {
#ifdef CLUSTER_TOMCAT
    Ptr<ProxyMutex> m = action_.mutex;
    if (!m) {
      m = new_ProxyMutex();
    }
#else
    Ptr<ProxyMutex> m = action_.mutex;
#endif

    // Establish the local side of the VC connection
    MUTEX_TRY_LOCK(lock, m, t);
    if (!lock) {
      t->schedule_in(this, CLUSTER_CONNECT_RETRY);
      return EVENT_DONE;
    }
    if (!ch) {
      if (action_.continuation) {
        action_.continuation->handleEvent(CLUSTER_EVENT_OPEN_FAILED, (void *) -ECLUSTER_NO_MACHINE);
        clusterVCAllocator_free(this);
        return EVENT_DONE;
      } else {
        // if we have been invoked immediately
        clusterVCAllocator_free(this);
        return -1;
      }
    }

    channel = ch->alloc_channel(this);
    if (channel < 0) {
      if (action_.continuation) {
        action_.continuation->handleEvent(CLUSTER_EVENT_OPEN_FAILED, (void *) -ECLUSTER_NOMORE_CHANNELS);
        clusterVCAllocator_free(this);
        return EVENT_DONE;
      } else {
        // if we have been invoked immediately
        clusterVCAllocator_free(this);
        return -1;
      }

    } else {
      Debug(CL_TRACE, "VC start alloc local chan=%d VC=%p", channel, this);
      if (new_connect_read)
        this->pending_remote_fill = 1;
    }

  } else {
    // Establish the remote side of the VC connection
    if ((status = ch->alloc_channel(this, channel)) < 0) {
      Debug(CL_TRACE, "VC start alloc remote failed chan=%d VC=%p", channel, this);
      clusterVCAllocator_free(this);
      return status;            // Channel active or no more channels
    } else {
      Debug(CL_TRACE, "VC start alloc remote chan=%d VC=%p", channel, this);
      if (new_connect_read)
        this->pending_remote_fill = 1;
      this->iov_map = CLUSTER_IOV_NONE; // disable connect timeout
    }
  }
  cluster_set_priority(ch, &read, CLUSTER_INITIAL_PRIORITY);
  cluster_schedule(ch, this, &read);
  cluster_set_priority(ch, &write, CLUSTER_INITIAL_PRIORITY);
  cluster_schedule(ch, this, &write);
  if (action_.continuation) {
    action_.continuation->handleEvent(CLUSTER_EVENT_OPEN, this);
  }
  mutex = NULL;
  return EVENT_DONE;
}

int
ClusterVConnection::was_closed()
{
  return (closed && !close_disabled);
}

void
ClusterVConnection::allow_close()
{
  close_disabled = 0;
}

void
ClusterVConnection::disable_close()
{
  close_disabled = 1;
}

int
ClusterVConnection::was_remote_closed()
{
  if (!byte_bank_q.head && !remote_close_disabled)
    return remote_closed;
  else
    return 0;
}

void
ClusterVConnection::allow_remote_close()
{
  remote_close_disabled = 0;
}

bool ClusterVConnection::schedule_write()
{
  //
  // Schedule write if we have all data or current write data is
  // at least DEFAULT_MAX_BUFFER_SIZE.
  //
  if (write_list) {
    if ((closed < 0) || remote_closed) {
      // User aborted connection, dump data.

      write_list = 0;
      write_list_tail = 0;
      write_list_bytes = 0;

      return false;
    }

    if (closed || (write_list_bytes >= DEFAULT_MAX_BUFFER_SIZE)) {
      // No more data to write or buffer list is full, start write
      return true;
    } else {
      // Buffer list is not full, defer write
      return false;
    }
  } else {
    return false;
  }
}

void
ClusterVConnection::set_type(int options)
{
  new_connect_read = (options & CLUSTER_OPT_CONN_READ) ? 1 : 0;
  if (new_connect_read) {
    pending_remote_fill = 1;
  } else {
    pending_remote_fill = 0;
  }
}

// Overide functions in base class VConnection.
bool ClusterVConnection::get_data(int id, void *data)
{
  switch (id) {
  case CACHE_DATA_HTTP_INFO:
    {
      ink_release_assert(!"ClusterVConnection::get_data CACHE_DATA_HTTP_INFO not supported");
    }
  case CACHE_DATA_KEY:
    {
      ink_release_assert(!"ClusterVConnection::get_data CACHE_DATA_KEY not supported");
    }
  default:
    {
      ink_release_assert(!"ClusterVConnection::get_data invalid id");
    }
  }
  return false;
}

void
ClusterVConnection::get_http_info(CacheHTTPInfo ** info)
{
  *info = &alternate;
}

int64_t
ClusterVConnection::get_object_size()
{
  return alternate.object_size_get();
}

void
ClusterVConnection::set_http_info(CacheHTTPInfo * d)
{
  int flen, len;
  void *data;
  int res;
  SetChanDataMessage *m;
  SetChanDataMessage msg;

  //
  // set_http_info() is a mechanism to associate additional data with a
  // open_write() ClusterVConnection.  It is only allowed after a
  // successful open_write() and prior to issuing the do_io(VIO::WRITE).
  // Cache semantics dictate that set_http_info() be established prior
  // to transferring any data on the ClusterVConnection.
  //
  ink_release_assert(this->write.vio.op == VIO::NONE);  // not true if do_io()
  //   already done
  ink_release_assert(this->read.vio.op == VIO::NONE);   // should always be true

  int vers = SetChanDataMessage::protoToVersion(ch->machine->msg_proto_major);
  if (vers == SetChanDataMessage::SET_CHANNEL_DATA_MESSAGE_VERSION) {
    flen = SetChanDataMessage::sizeof_fixedlen_msg();
  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"ClusterVConnection::set_http_info() bad msg version");
  }

  // Create message and marshal data.

  CacheHTTPInfo *r = d;
  len = r->marshal_length();
  data = (void *) ALLOCA_DOUBLE(flen + len);
  memcpy((char *) data, (char *) &msg, sizeof(msg));
  m = (SetChanDataMessage *) data;
  m->data_type = CACHE_DATA_HTTP_INFO;

  char *p = (char *) m + flen;
  res = r->marshal(p, len);
  if (res < 0) {
    r->destroy();
    return;
  }
  r->destroy();

  m->channel = channel;
  m->sequence_number = token.sequence_number;

  // note pending set_data() msgs on VC.
  ink_atomic_increment(&n_set_data_msgs, 1);

  clusterProcessor.invoke_remote(ch, SET_CHANNEL_DATA_CLUSTER_FUNCTION, data, flen + len);
}

bool ClusterVConnection::set_pin_in_cache(time_t t)
{
  SetChanPinMessage msg;

  //
  // set_pin_in_cache() is a mechanism to set an attribute on a
  // open_write() ClusterVConnection.  It is only allowed after a
  // successful open_write() and prior to issuing the do_io(VIO::WRITE).
  //
  ink_release_assert(this->write.vio.op == VIO::NONE);  // not true if do_io()
  //   already done
  ink_release_assert(this->read.vio.op == VIO::NONE);   // should always be true
  time_pin = t;

  int vers = SetChanPinMessage::protoToVersion(ch->machine->msg_proto_major);

  if (vers == SetChanPinMessage::SET_CHANNEL_PIN_MESSAGE_VERSION) {
    SetChanPinMessage::sizeof_fixedlen_msg();
  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"ClusterVConnection::set_pin_in_cache() bad msg " "version");
  }
  msg.channel = channel;
  msg.sequence_number = token.sequence_number;
  msg.pin_time = time_pin;

  // note pending set_data() msgs on VC.
  ink_atomic_increment(&n_set_data_msgs, 1);

  clusterProcessor.invoke_remote(ch, SET_CHANNEL_PIN_CLUSTER_FUNCTION, (char *) &msg, sizeof(msg));
  return true;
}

time_t ClusterVConnection::get_pin_in_cache()
{
  return time_pin;
}

bool ClusterVConnection::set_disk_io_priority(int priority)
{
  SetChanPriorityMessage msg;

  //
  // set_disk_io_priority() is a mechanism to set an attribute on a
  // open_write() ClusterVConnection.  It is only allowed after a
  // successful open_write() and prior to issuing the do_io(VIO::WRITE).
  //
  ink_release_assert(this->write.vio.op == VIO::NONE);  // not true if do_io()
  //   already done
  ink_release_assert(this->read.vio.op == VIO::NONE);   // should always be true
  disk_io_priority = priority;

  int vers = SetChanPriorityMessage::protoToVersion(ch->machine->msg_proto_major);

  if (vers == SetChanPriorityMessage::SET_CHANNEL_PRIORITY_MESSAGE_VERSION) {
    SetChanPriorityMessage::sizeof_fixedlen_msg();
  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"ClusterVConnection::set_disk_io_priority() bad msg " "version");
  }
  msg.channel = channel;
  msg.sequence_number = token.sequence_number;
  msg.disk_priority = priority;

  // note pending set_data() msgs on VC.
  ink_atomic_increment(&n_set_data_msgs, 1);

  clusterProcessor.invoke_remote(ch, SET_CHANNEL_PRIORITY_CLUSTER_FUNCTION, (char *) &msg, sizeof(msg));
  return true;
}

int
ClusterVConnection::get_disk_io_priority()
{
  return disk_io_priority;
}

void
ClusterVConnection::reenable(VIO * vio)
{
  ClusterVConnectionBase::reenable(vio);
  if (type == VC_CLUSTER_WRITE) {
    type = VC_CLUSTER_WRITE_SCHEDULE;
    ink_atomiclist_push(&ch->write_vcs_ready, (void *)this);
  }
}



ClusterCacheVC::ClusterCacheVC() {
  size_to_init = sizeof(ClusterCacheVC) - (size_t) & ((ClusterCacheVC *) 0)->vio;
  memset((char *) &vio, 0, size_to_init);
}

int
ClusterCacheVC::handleRead(int event, void *data)
{
  ink_debug_assert(!in_progress && !remote_closed);
  PUSH_HANDLER(&ClusterCacheVC::openReadReadDone);
  if (vio.nbytes > 0 && total_len == 0) {
    SetIOReadMessage msg;
    msg.nbytes = vio.nbytes;
    msg.offset = seek_to;
    if (!cluster_send_message(cs, -CLUSTER_CACHE_DATA_READ_BEGIN, (char *) &msg,
        sizeof(msg), PRIORITY_HIGH)) {
      in_progress = true;
      cluster_set_events(cs, RESPONSE_EVENT_NOTIFY_DEALER);
      return EVENT_CONT;
    }
    goto Lfailed;
  }

  if (!cluster_send_message(cs, -CLUSTER_CACHE_DATA_READ_REENABLE, NULL, 0,
      PRIORITY_HIGH)) {
    in_progress = true;
    cluster_set_events(cs, RESPONSE_EVENT_NOTIFY_DEALER);
    return EVENT_CONT;
  }
  Lfailed:
  CLUSTER_CACHE_VC_CLOSE_SESSION;
  return calluser(VC_EVENT_ERROR);
}

int
ClusterCacheVC::openReadReadDone(int event, void *data)
{
  cancel_trigger();
  ink_debug_assert(in_progress);
  in_progress = false;
  POP_HANDLER;

  switch (event) {
    case CLUSTER_CACHE_DATA_ERROR:
    {
      ClusterCont *cc = (ClusterCont *) data;
      ink_assert(cc && cc->data_len > 0);
      remote_closed = true;
      event = *(int *) cc->data->start();
      break;
    }
    case CLUSTER_CACHE_DATA_READ_DONE:
    {
      ClusterCont *cc = (ClusterCont *) data;
      ink_debug_assert(cc && d_len == 0);

      d_len = cc->data_len;
      total_len += d_len;
      blocks = cc->data;
      if (total_len >= vio.nbytes)
        remote_closed = true;
      break;
    }
    case CLUSTER_INTERNEL_ERROR:
    default:
      event = VC_EVENT_ERROR;
      remote_closed = true;
      break;
  }

  if (closed) {
    if (!remote_closed)
      cluster_send_message(cs, CLUSTER_CACHE_DATA_ABORT, NULL, 0, PRIORITY_HIGH);

    free_ClusterCacheVC(this);
    return EVENT_DONE;
  }
  // recevied data from cluster

  return handleEvent(event, data);
}

int
ClusterCacheVC::openReadStart(int event, void *data)
{
  ink_assert(in_progress);
  in_progress = false;
  if (_action.cancelled) {
    if (!remote_closed)
      cluster_send_message(cs, CLUSTER_CACHE_DATA_ABORT, NULL, 0, PRIORITY_HIGH);
    free_ClusterCacheVC(this);
    return EVENT_DONE;
  }
  if (event != CACHE_EVENT_OPEN_READ) {
    // prevent further trigger
    remote_closed = true;
    CLUSTER_CACHE_VC_CLOSE_SESSION;
    _action.continuation->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, data);
    free_ClusterCacheVC(this);
    return EVENT_DONE;
  }

  SET_HANDLER(&ClusterCacheVC::openReadMain);
  callcont(CACHE_EVENT_OPEN_READ);
  return EVENT_CONT;
}
int
ClusterCacheVC::openReadMain(int event, void *e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  cancel_trigger();
  ink_assert(!in_progress);
  if (event == VC_EVENT_ERROR || event == VC_EVENT_EOS) {
    remote_closed = true;
    CLUSTER_CACHE_VC_CLOSE_SESSION;
    return calluser(event);
  }

  int64_t bytes = d_len;
  int64_t ntodo = vio.ntodo();
  if (ntodo <= 0)
    return EVENT_CONT;
  if (vio.buffer.mbuf->max_read_avail() > vio.buffer.writer()->water_mark && vio.ndone) // initiate read of first block
    return EVENT_CONT;
  if (!blocks && vio.ntodo() > 0)
    goto Lread;

  if (bytes > vio.ntodo())
    bytes = vio.ntodo();
  vio.buffer.mbuf->append_block(blocks);
  vio.ndone += bytes;
  blocks = NULL;
  d_len -= bytes;

  if (vio.ntodo() <= 0)
    return calluser(VC_EVENT_READ_COMPLETE);
  else {
    if (calluser(VC_EVENT_READ_READY) == EVENT_DONE)
      return EVENT_DONE;
    // we have to keep reading until we give the user all the
    // bytes it wanted or we hit the watermark.
    if (vio.ntodo() > 0 && !vio.buffer.writer()->high_water())
      goto Lread;
    return EVENT_CONT;
  }
Lread:
  if (vio.ndone >= (int64_t) doc_len) {
    // reached the end of the document and the user still wants more
    return calluser(VC_EVENT_EOS);
  }
  // if the state machine calls reenable on the callback from the cache,
  // we set up a schedule_imm event. The openReadReadDone discards
  // EVENT_IMMEDIATE events. So, we have to cancel that trigger and set
  // a new EVENT_INTERVAL event.
  cancel_trigger();
  return handleRead(event, e);
}


//int
//ClusterCacheVC::handleWrite(int event, void *data)
//{
//  in_progress = true;
//  PUSH_HANDLER(&ClusterCacheVC::openWriteWriteDone);
//  ClusterBufferReader *reader = new_ClusterBufferReader();
//
//  if (!cluster_send_message(cs, CLUSTER_CACHE_DATA_WRITE_DONE, reader, -1, priority))
//      return EVENT_CONT;
//  free_ClusterBufferReader(reader);
//  return calluser(VC_EVENT_ERROR);
//}
//
//int
//ClusterCacheVC::openWriteWriteDone(int event, void *data)
//{
//  // process the data
//  POP_HANDLER;
//  return handleEvent(event, data);
//}

int
ClusterCacheVC::openWriteStart(int event, void *data)
{
  ink_assert(in_progress);
  in_progress = false;
  if (_action.cancelled) {
    if (!remote_closed)
      cluster_send_message(cs, CLUSTER_CACHE_DATA_ABORT, NULL, 0, PRIORITY_HIGH);
    free_ClusterCacheVC(this);
    return EVENT_DONE;
  }
  // process the data
  if (event != CACHE_EVENT_OPEN_WRITE) {
    // prevent further trigger
    remote_closed = true;
    CLUSTER_CACHE_VC_CLOSE_SESSION;
    _action.continuation->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, data);
    free_ClusterCacheVC(this);
    return EVENT_DONE;
  }
  SET_HANDLER(&ClusterCacheVC::openWriteMain);
  return callcont(CACHE_EVENT_OPEN_WRITE);
}
int
ClusterCacheVC::openWriteMain(int event, void *data)
{
  NOWARN_UNUSED(event);
  cancel_trigger();
  ink_debug_assert(!in_progress);

Lagain:
  if (remote_closed) {
    if (calluser(VC_EVENT_ERROR) == EVENT_DONE)
      return EVENT_DONE;
    return EVENT_CONT;
  }

  if (!vio.buffer.writer()) {
    if (calluser(VC_EVENT_WRITE_READY) == EVENT_DONE)
      return EVENT_DONE;
    if (!vio.buffer.writer())
      return EVENT_CONT;
  }

  int64_t ntodo = vio.ntodo();

  if (ntodo <= 0) {
    if (calluser(VC_EVENT_WRITE_COMPLETE) == EVENT_DONE)
      return EVENT_DONE;
    ink_assert(!"close expected after write COMPLETE");
    if (vio.ntodo() <= 0)
      return EVENT_CONT;
  }

  ntodo = vio.ntodo() + length;
  int64_t total_avail = vio.buffer.reader()->read_avail();
  int64_t avail = total_avail;
  int64_t towrite = avail + length;
  if (towrite > ntodo) {
    avail -= (towrite - ntodo);
    towrite = ntodo;
  }

  if (!blocks && towrite) {
    blocks = vio.buffer.reader()->block;
    offset = vio.buffer.reader()->start_offset;
  }

  if (avail > 0) {
    vio.buffer.reader()->consume(avail);
    vio.ndone += avail;
    total_len += avail;
  }

  ink_assert(towrite >= 0);
  length = towrite;

  int flen = cache_config_target_fragment_size;

  while (length >= flen) {
    IOBufferBlock *r = clone_IOBufferBlockList(blocks, offset, flen);
    blocks = iobufferblock_skip(blocks, &offset, &length, flen);

    remote_closed = cluster_send_message(cs, CLUSTER_CACHE_DATA_WRITE_DONE, r, -1,
        priority);
    if (remote_closed)
      goto Lagain;

    data_sent += flen;
    Debug("data_sent", "sent bytes %d, reminds %"PRId64"", flen, length);
  }
  // for the read_from_writer work better,
  // especailly the slow original
  flen = CLUSTER_WRITE_MIN_SIZE;
  if (length >= flen || (vio.ntodo() <= 0 && length > 0)) {
    data_sent += length;
    IOBufferBlock *r = clone_IOBufferBlockList(blocks, offset, length);
    blocks = iobufferblock_skip(blocks, &offset, &length, length);
    remote_closed = cluster_send_message(cs, CLUSTER_CACHE_DATA_WRITE_DONE, r,
              -1, priority);
    if (remote_closed)
      goto Lagain;
    Debug("data_sent", "sent bytes %d, reminds %"PRId64"", flen, length);
  }

  if (vio.ntodo() <= 0) {
    ink_debug_assert(length == 0 && total_len == vio.nbytes);
    return calluser(VC_EVENT_WRITE_COMPLETE);
  }
  return calluser(VC_EVENT_WRITE_READY);
}

int
ClusterCacheVC::removeEvent(int event, void *data)
{
  ink_debug_assert(in_progress);
  in_progress = false;
  remote_closed = true;
  CLUSTER_CACHE_VC_CLOSE_SESSION;
  if (!_action.cancelled)
    _action.continuation->handleEvent(event, data);
  free_ClusterCacheVC(this);
  return EVENT_DONE;
}

VIO *
ClusterCacheVC::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *abuf)
{
  ink_assert(vio.op == VIO::READ && alternate.valid());
  vio.buffer.writer_for(abuf);
  vio.set_continuation(c);
  vio.ndone = 0;
  vio.nbytes = nbytes;
  vio.vc_server = this;
  seek_to = 0;
  ink_assert(c->mutex->thread_holding);

  ink_assert(!in_progress);
  if (!trigger && !recursive)
    trigger = c->mutex->thread_holding->schedule_imm_local(this);
  return &vio;
}

VIO *
ClusterCacheVC::do_io_pread(Continuation *c, int64_t nbytes, MIOBuffer *abuf, int64_t offset)
{
  ink_assert(vio.op == VIO::READ && alternate.valid());
  vio.buffer.writer_for(abuf);
  vio.set_continuation(c);
  vio.ndone = 0;
  vio.nbytes = nbytes;
  vio.vc_server = this;
  seek_to = offset;
  ink_assert(c->mutex->thread_holding);

  ink_assert(!in_progress);
  if (!trigger && !recursive)
    trigger = c->mutex->thread_holding->schedule_imm_local(this);
  return &vio;
}

VIO *
ClusterCacheVC::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *abuf, bool owner)
{
  ink_assert(vio.op == VIO::WRITE);
  ink_assert(!owner && !in_progress);
  vio.buffer.reader_for(abuf);
  vio.set_continuation(c);
  vio.ndone = 0;
  vio.nbytes = nbytes;
  doc_len = nbytes; // note: the doc_len maybe not the real length of the body
  vio.vc_server = this;
  ink_assert(c->mutex->thread_holding);

  if (nbytes < (1 << 20))
    priority = PRIORITY_MID;
  else
    priority = PRIORITY_LOW;

  CacheHTTPInfo *r = &alternate;
  SetIOWriteMessage msg;
  msg.nbytes = nbytes;
  int len = r->valid() ? r->marshal_length() : 0;
  msg.hdr_len = len;
  ink_debug_assert(total_len == 0);
  ink_debug_assert((frag_type == CACHE_FRAG_TYPE_HTTP && len > 0) ||
      (frag_type != CACHE_FRAG_TYPE_HTTP && len == 0));

  if (len > 0) {
    Ptr<IOBufferData> data = new_IOBufferData(iobuffer_size_to_index(sizeof msg + len, MAX_BUFFER_SIZE_INDEX));
    memcpy((char *) data->data(), &msg, sizeof(msg));
    char *p = (char *) data->data() + sizeof msg;
    int res = r->marshal(p, len);
    ink_assert(res >= 0);
    IOBufferBlock *ret = new_IOBufferBlock(data, sizeof msg + len, 0);
    ret->_buf_end = ret->_end;
    remote_closed = cluster_send_message(cs, CLUSTER_CACHE_DATA_WRITE_BEGIN, ret, -1, priority);
  } else
    remote_closed = cluster_send_message(cs, -CLUSTER_CACHE_DATA_WRITE_BEGIN, &msg, sizeof msg, priority);

  if (!trigger && !recursive)
    trigger = c->mutex->thread_holding->schedule_imm_local(this);
  return &vio;
}

void
ClusterCacheVC::do_io_close(int alerrno)
{
  ink_debug_assert(mutex->thread_holding == this_ethread());
  int previous_closed = closed;
  closed = (alerrno == -1) ? 1 : -1;    // Stupid default arguments
  DDebug("cache_close", "do_io_close %p %d %d", this, alerrno, closed);

  // special case: to cache 0 bytes document
  if (f.force_empty)
    closed = 1;

  if (!remote_closed) {
    if (closed > 0 && vio.op == VIO::WRITE) {
      if ((f.update && vio.nbytes == 0) || f.force_empty) {
        //header only update
        //
        if (frag_type == CACHE_FRAG_TYPE_HTTP) {
          if (alternate.valid()) {
            SetIOCloseMessage msg;
            msg.h_len = alternate.marshal_length();
            msg.d_len = 0;
            msg.total_len = 0;

            Ptr<IOBufferData> d = new_IOBufferData(
                iobuffer_size_to_index(sizeof msg + msg.h_len));
            char *data = d->data();
            memcpy(data, &msg, sizeof msg);

            int res = alternate.marshal((char *) data + sizeof msg, msg.h_len);
            ink_assert(res >= 0 && res <= msg.h_len);

            IOBufferBlock *ret = new_IOBufferBlock(d, sizeof msg + msg.h_len, 0);
            ret->_buf_end = ret->_end;

            remote_closed = cluster_send_message(cs,
                -CLUSTER_CACHE_HEADER_ONLY_UPDATE, ret, -1, PRIORITY_HIGH);
          } else
            remote_closed = cluster_send_message(cs, CLUSTER_CACHE_DATA_ABORT, NULL, 0, PRIORITY_HIGH);
        } else {
          remote_closed = cluster_send_message(cs, -CLUSTER_CACHE_DATA_CLOSE,
              &total_len, sizeof total_len, priority);
        }

        goto Lfree;
      } else if ((total_len < vio.nbytes) || length > 0) {
        int64_t ntodo = vio.ntodo() + length;
        int64_t total_avail = vio.buffer.reader()->read_avail();
        int64_t avail = total_avail;
        int64_t towrite = avail + length;
        if (towrite > ntodo) {
          avail -= (towrite - ntodo);
          towrite = ntodo;
        }

        if (!blocks && towrite) {
          blocks = vio.buffer.reader()->block;
          offset = vio.buffer.reader()->start_offset;
        }

        if (avail > 0) {
          vio.buffer.reader()->consume(avail);
          vio.ndone += avail;
          total_len += avail;
        }

        if (vio.ntodo() > 0) {
          Warning("writer closed success but still want more data");
          remote_closed = cluster_send_message(cs, CLUSTER_CACHE_DATA_ABORT, NULL, 0,
                        priority);
          goto Lfree;
        }

        length = towrite;
        ink_debug_assert(total_len == vio.nbytes);
        int flen = cache_config_target_fragment_size;
        while (length >= flen) {
          IOBufferBlock *ret = clone_IOBufferBlockList(blocks, offset, flen);
          blocks = iobufferblock_skip(blocks, &offset, &length, flen);

          remote_closed = cluster_send_message(cs, CLUSTER_CACHE_DATA_WRITE_DONE, ret,
              -1, priority);
          if (remote_closed)
            goto Lfree;

          data_sent += flen;
          Debug("data_sent", "sent bytes %d, reminds %"PRId64"", flen, length);
        }

        if (length > 0) {
          data_sent += length;
          IOBufferBlock *ret = clone_IOBufferBlockList(blocks, offset, length);
          blocks = iobufferblock_skip(blocks, &offset, &length, length);
          remote_closed = cluster_send_message(cs, CLUSTER_CACHE_DATA_WRITE_DONE, ret, -1,
              priority);
          if (remote_closed)
            goto Lfree;
          Debug("data_sent", "sent bytes done: %"PRId64", reminds %"PRId64"", data_sent, length);
        }
      }

      if (doc_len != vio.nbytes) {
        // for trunk
        ink_assert(total_len == vio.nbytes && length == 0);
        remote_closed = cluster_send_message(cs, CLUSTER_CACHE_DATA_CLOSE,
            &total_len, sizeof total_len, priority);
        goto Lfree;
      }
      ink_assert(data_sent == total_len);
    }

    if (closed < 0 && vio.op == VIO::WRITE)
      remote_closed = cluster_send_message(cs, CLUSTER_CACHE_DATA_ABORT, NULL, 0, PRIORITY_HIGH);

    if (vio.op == VIO::READ && !in_progress) {
      remote_closed = cluster_send_message(cs, CLUSTER_CACHE_DATA_ABORT, NULL, 0, PRIORITY_HIGH);
    }
  }
Lfree:
  if (!previous_closed && !recursive && !in_progress) {
    free_ClusterCacheVC(this);
  }
}

void
ClusterCacheVC::reenable(VIO *avio)
{
  DDebug("cache_reenable", "reenable %p, trigger %p, in_progress %d", this, trigger, in_progress);
  (void) avio;
  ink_assert(avio->mutex->thread_holding);
  if (!trigger && !in_progress) {
    trigger = avio->mutex->thread_holding->schedule_imm_local(this);
  }
}

void
ClusterCacheVC::reenable_re(VIO *avio)
{
  DDebug("cache_reenable", "reenable %p", this);
  (void) avio;
  ink_assert(avio->mutex->thread_holding);

  if (!trigger) {
    if (!in_progress && !recursive) {
      handleEvent(EVENT_NONE, (void *) 0);
    } else if (!in_progress)
      trigger = avio->mutex->thread_holding->schedule_imm_local(this);
  }
}


// End of ClusterVConnection.cc
