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

#include "HCHandler.h"
#include "HCSM.h"

ClassAllocator<HCHandler> hcHandlerAllocator("HCHandlerAllocator");

static int next_id = 0;

void
HCHandler::init(HCEntry *entry)
{
  id = (int64_t) ink_atomic_increment((&next_id), 1);
  HC_STATE_ENTER(&HCHandler::init);
  hc_entry = entry;
  mutex = new_ProxyMutex();
  SET_HANDLER(&HCHandler::main_event);
}

int
HCHandler::main_event(int event, void *data)
{
  HC_STATE_ENTER(&HCHandler::main_event);
  handle_dns_lookup();

  return 0;
}

int
HCHandler::state_dns_lookup(int event, void *data)
{
  HC_STATE_ENTER(&HCHandler::state_dns_lookup);
  SET_HANDLER(&HCHandler::main_event);
  switch (event) {
    case EVENT_HOST_DB_LOOKUP:
      process_hostdb_info((HostDBInfo *) data);
      break;
    case EVENT_HOST_DB_IP_REMOVED:
      ink_assert(!"Unexpected event from HostDB");
      break;
    default:
      ink_assert(!"Unexpected event");
      break;
  }

  return 0;
}

void
HCHandler::handle_dns_lookup()
{
  HC_STATE_ENTER(&HCHandler::handle_dns_lookup);
  SET_HANDLER(&HCHandler::state_dns_lookup);

  char new_host[MAXDNAME];
  strncpy(new_host, hc_entry->hostname, MAXDNAME);
  int port = hc_entry->port;
  hostDBProcessor.getbyname_imm_use_cache(this, (process_hostdb_info_pfn) &HCHandler::process_hostdb_info, new_host, 0, port);
}

void
HCHandler::process_hostdb_info(HostDBInfo *r)
{
  HC_STATE_ENTER(&HCHandler::process_hostdb_info);
  SET_HANDLER(&HCHandler::main_event);
  if (r) {
    if (r->round_robin) {
      HostDBRoundRobin *rr = r->rr();
      if (NULL == rr) {
        Error("healthcheck [%" PRId64 "] bad round_robin, hostname %s", id, hc_entry->hostname);
        return;
      }
      for (int i = 0; i < rr->good; ++i) {
        HCSM *hcsm = HCSM::allocate();
        hcsm->init(hc_entry, &rr->info[i]);
        eventProcessor.schedule_imm(hcsm, ET_TASK);
      }
    } else {
      HCSM *hcsm = HCSM::allocate();
      hcsm->init(hc_entry, r);
      eventProcessor.schedule_imm(hcsm, ET_TASK);
    }
  } else {
    Debug("healthcheck", "[%" PRId64 "DNS lookup faild for %s", id, hc_entry->hostname);
  }
}
