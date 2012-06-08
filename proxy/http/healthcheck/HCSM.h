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

#ifndef _HEALTHCHECK_STATEMACHINE_H_
#define _HEALTHCHECK_STATEMACHINE_H_

#include "I_HC.h"
#include "HCUtil.h"

class HCSM;
extern ClassAllocator<HCSM> hcsmAllocator;
class HCSM: public Continuation
{
public:
  HCSM() {}
  virtual ~HCSM() {}
  static HCSM* allocate()
  {
    return hcsmAllocator.alloc();
  }
  void init(HCEntry *entry, HostDBInfo *r);
  void destroy();

  int main_event(int event, void *data);
  int state_con2os(int event, void *data);
  int state_send_req(int event, void *data);
  int state_read_res(int event, void *data);
  void handle_con2os();
  void handle_send_req();
  void handle_read_res();

  OverridableHttpConfigParams txn_conf;
private:
  void attach_server_session(HttpServerSession *s);

  int64_t id;
  int server_response_hdr_bytes;

  HCEntry *hc_entry;
  HostDBInfo *hostdb_info;

  VCEntry *vc_entry;
  HttpServerSession *server_session;
  IOBufferReader *buffer_reader;

  HttpConfigParams *http_config_param;
  HTTPHdr res_hdr;
  HTTPParser http_parser;
private:
  HCSM(const HCSM &hcsm);
  HCSM& operator=(const HCSM &hcsm);
};

#endif
