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

#include "HCSM.h"

static int next_id = 0;

ClassAllocator<HCSM> hcsmAllocator("HCSMAllocator");

static int
write_header(HTTPHdr * h, MIOBuffer * b)
{
  int bufindex;
  int dumpoffset;
  int done, tmp;
  IOBufferBlock *block;

  dumpoffset = 0;
  do {
    bufindex = 0;
    tmp = dumpoffset;
    block = b->get_current_block();
    ink_assert(block->write_avail() > 0);
    done = h->print(block->start(), block->write_avail(), &bufindex, &tmp);
    dumpoffset += bufindex;
    ink_assert(bufindex > 0);
    b->fill(bufindex);
    if (!done) {
      b->add_block();
    }
  } while (!done);

  return dumpoffset;
}

void
HCSM::init(HCEntry *entry, HostDBInfo *r)
{
  id = (int64_t) ink_atomic_increment((&next_id), 1);
  HC_STATE_ENTER(&HCSM::init);
  hc_entry = entry;
  hostdb_info = r;
  server_session = NULL;
  buffer_reader = NULL;
  mutex = new_ProxyMutex();
  http_config_param = HttpConfig::acquire();
  memcpy(&txn_conf, &(http_config_param->oride), sizeof(txn_conf));
  http_parser_init(&http_parser);
  res_hdr.create(HTTP_TYPE_RESPONSE);
  vc_entry = VCEntry::allocate();
  server_response_hdr_bytes = 0;
  SET_HANDLER(&HCSM::main_event);
}

void
HCSM::destroy()
{
  HC_STATE_ENTER(&HCSM::destroy);
  mutex.clear();
  HttpConfig::release(http_config_param);
  http_parser_clear(&http_parser);
  res_hdr.destroy();
  vc_entry->destroy();
  hcsmAllocator.free(this);
}

int
HCSM::main_event(int event, void *data)
{
  HC_STATE_ENTER(&HCSM::main_event);
  Debug("healthcheck", "[%" PRId64 "send url to os %s with port %d:\n%s", id, hc_entry->hostname, hc_entry->port, hc_entry->req_hdr.url_string_get());
  hostdb_info->hc_switch = hc_entry->hc_switch;
  if (true == hostdb_info->hc_switch) {
    handle_con2os();
  } else {
    if (NULL != server_session) {
      server_session->do_io_close();
      server_session = NULL;
    }
    destroy();
  }
  return 0;
}

int
HCSM::state_con2os(int event, void *data)
{
  HC_STATE_ENTER(&HCSM::state_con2os);
  NetVConnection *new_vc = (NetVConnection *) data;
  HttpServerSession *session = THREAD_ALLOC_INIT(httpServerSessionAllocator, mutex->thread_holding);
  switch (event) {
  case NET_EVENT_OPEN:
    if (txn_conf.origin_max_connections > 0 || http_config_param->origin_min_keep_alive_connections > 0) {
      session->enable_origin_connection_limiting = true;
    }
    session->server_ip.sa = *hostdb_info->ip();
    session->new_connection(new_vc);
    //session->server_port = hc_entry->port;
    session->state = HSS_ACTIVE;
    attach_server_session(session);
    handle_send_req();
    break;
  case EVENT_INTERVAL:
    handle_con2os();
    break;
  default:
    if (NULL != server_session) {
      server_session->do_io_close();
      server_session = NULL;
    }
    destroy();
    break;
  }
  return 0;
}

int
HCSM::state_send_req(int event, void *data)
{
  HC_STATE_ENTER(&HCSM::state_send_req);
  switch (event) {
  case VC_EVENT_WRITE_READY:
    vc_entry->write_vio->reenable();
    return 0;
  case VC_EVENT_WRITE_COMPLETE:
    free_MIOBuffer(vc_entry->write_buffer);
    vc_entry->write_buffer = NULL;
    handle_read_res();
    return 0;
  case VC_EVENT_READ_READY:
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  default:
    if (NULL != server_session) {
      server_session->do_io_close();
      server_session = NULL;
    }
    destroy();
    break;
  }

  return 0;
}

int
HCSM::state_read_res(int event, void *data)
{
  HC_STATE_ENTER(&HCSM::state_read_res);
  switch(event){
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
    break;
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  default:
    if (NULL != server_session) {
      server_session->do_io_close();
      server_session = NULL;
    }
    destroy();
    return 0;
  }

  if (0 == server_response_hdr_bytes) {
    server_session->get_netvc()->set_inactivity_timeout(HRTIME_SECONDS(txn_conf.transaction_no_activity_timeout_out));
  }
  int bytes_used = 0;
  int state = res_hdr.parse_resp(&http_parser, buffer_reader, &bytes_used, false);
  server_response_hdr_bytes += bytes_used;
  if (PARSE_DONE == state && res_hdr.version_get() == HTTPVersion(0, 9) && server_session->transact_count > 1) {
    state = PARSE_ERROR;
  }
  if (server_response_hdr_bytes > http_config_param->response_hdr_max_size) {
    state = PARSE_ERROR;
  }
  if (state != PARSE_CONT) {
    vc_entry->read_vio->nbytes = vc_entry->read_vio->ndone;
    http_parser_clear(&http_parser);
  }

  HTTPStatus ret = res_hdr.status_get();
  switch (state) {
  case PARSE_DONE:
    if (HTTP_STATUS_OK <= ret  && ret <= HTTP_STATUS_PARTIAL_CONTENT) {
      hostdb_info->hc_state = true;
      hostdb_info->hc_ttl = hc_entry->ttl;
      hostdb_info->refresh_hc();
      char hostname[MAXDNAME];
      strncpy(hostname, hc_entry->hostname, MAXDNAME);
      hostname[MAXDNAME -1] = '\0';
      --server_session->server_trans_stat;
      server_session->attach_hostname(hostname);
      server_session->release();
      server_session = NULL;
    } else {
      hostdb_info->hc_state = false;
      server_session->do_io_close();
      server_session = NULL;
    }
    destroy();
    break;
  case PARSE_CONT:
    vc_entry->read_vio->reenable();
    return VC_EVENT_CONT;
  case PARSE_ERROR:
  default:
    if (NULL != server_session) {
      server_session->do_io_close();
      server_session = NULL;
    }
    destroy();
    break;
  }

  return 0;
}

void
HCSM::handle_con2os()
{
  HC_STATE_ENTER(&HCSM::handle_con2os);
  SET_HANDLER(&HCSM::state_con2os);
  HttpServerSession *session = NULL;
  //sockaddr const* ip = hostdb_info->ip();
  IpEndpoint ip;
  ip.sa = *hostdb_info->ip();
  int port = hc_entry->port;
  ats_ip_port_cast(&ip) = htons(port);
  char hostname[MAXDNAME];
  strncpy(hostname, hc_entry->hostname, MAXDNAME);
  if (http_config_param->oride.share_server_sessions) {
    session = httpSessionManager.acquire_session_hc(&ip.sa, hostname, this);
  }
  if (NULL != session) {
    attach_server_session(session);
    handle_send_req();
  } else {
    if (http_config_param->server_max_connections > 0) {
      int64_t sum;
      HTTP_READ_GLOBAL_DYN_SUM(http_current_server_connections_stat, sum);
      if (sum >= http_config_param->server_max_connections) {
        SET_HANDLER(&HCSM::main_event);
        eventProcessor.schedule_in(this, HRTIME_MSECONDS(100));
        httpSessionManager.purge_keepalives();
        return ;
      }
    }
    if (txn_conf.origin_max_connections > 0) {
      ConnectionCount *connections = ConnectionCount::getInstance();
      if (connections->getCount(ip) >= txn_conf.origin_max_connections) {
        SET_HANDLER(&HCSM::main_event);
        eventProcessor.schedule_in(this, HRTIME_MSECONDS(100));
        return ;
      }
    }
    NetVCOptions opt;
    opt.f_blocking_connect = false;
    opt.set_sock_param(txn_conf.sock_recv_buffer_size_out, txn_conf.sock_send_buffer_size_out, txn_conf.sock_option_flag_out);
    netProcessor.connect_re(this, &ip.sa, &opt);
  }
}

void
HCSM::handle_send_req()
{
  HC_STATE_ENTER(&HCSM::handle_send_req);
  SET_HANDLER(&HCSM::state_send_req);
  int hdr_length;
  vc_entry->write_buffer = new_MIOBuffer(buffer_size_to_index(HTTP_HEADER_BUFFER_SIZE));
  IOBufferReader *buf_start = vc_entry->write_buffer->alloc_reader();
  hdr_length = write_header(&hc_entry->req_hdr, vc_entry->write_buffer);
  vc_entry->write_vio = vc_entry->vc->do_io_write(this, hdr_length, buf_start);
}

void
HCSM::handle_read_res()
{
  HC_STATE_ENTER(&HCSM::handle_read_res);
  SET_HANDLER(&HCSM::state_read_res);
  server_response_hdr_bytes = 0;
  res_hdr.destroy();
  res_hdr.create(HTTP_TYPE_RESPONSE);
  http_parser_clear(&http_parser);
  if (buffer_reader->read_avail() > 0) {
    state_read_res(VC_EVENT_READ_READY, vc_entry->read_vio);
  }

  if ((NULL != vc_entry) && (vc_entry->read_vio->nbytes == vc_entry->read_vio->ndone)) {
    vc_entry->read_vio = server_session->do_io_read(this, INT64_MAX, buffer_reader->mbuf);
  }
}

void
HCSM::attach_server_session(HttpServerSession *session)
{
  HC_STATE_ENTER(&HCSM::attach_server_session);
  vc_entry->clear();
  buffer_reader = NULL;

  server_session = session;
  ++server_session->transact_count;
  HTTP_INCREMENT_DYN_STAT(http_current_server_transactions_stat);
  ++server_session->server_trans_stat;

  vc_entry->vc = server_session;

  buffer_reader = server_session->get_reader();
  vc_entry->read_vio = server_session->do_io_read(this, INT64_MAX, server_session->read_buffer);
  server_session->do_io_write(this, 0, NULL);

  MgmtInt connect_timeout = txn_conf.connect_attempts_timeout;
  server_session->get_netvc()->set_inactivity_timeout(HRTIME_SECONDS(connect_timeout));
  server_session->get_netvc()->set_active_timeout(HRTIME_SECONDS(txn_conf.transaction_active_timeout_out));
}
