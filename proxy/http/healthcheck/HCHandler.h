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
#ifndef _HEALTHCHECK_HANDLER_H_
#define _HEALTHCHECK_HANDLER_H_

#include "I_HC.h"
#include "HCUtil.h"

class HCHandler;
extern ClassAllocator<HCHandler> hcHandlerAllocator;
class HCHandler: public Continuation
{
public:
  HCHandler() {}
  virtual ~HCHandler() {}
  static HCHandler* allocate()
  {
    return hcHandlerAllocator.alloc();
  }
  void init(HCEntry *hc_entry);
  int main_event(int event, void *data);
  int state_dns_lookup(int event, void *data);
  void handle_dns_lookup();
  void process_hostdb_info(HostDBInfo *r);
private:
  HCEntry *hc_entry;
  int64_t id;

  HCHandler(const HCHandler& hc_handler);
  HCHandler& operator=(const HCHandler& hc_handler);
};

#endif
