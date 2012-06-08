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

#ifndef _HEALTHCHECK_UTIL_H_
#define _HEALTHCHECK_UTIL_H_

#include <map>
#include <string>

#include "libts.h"
#include "HTTP.h"
#include "P_Net.h"
#include "I_Tasks.h"

#define HC_STATE_ENTER(state_name) { Debug("healthcheck", "[%" PRId64 "] [%s]", id, #state_name); }

extern int32_t healthcheck_default_ttl;
extern int32_t healthcheck_serve_stale_but_revalidate;
extern int healthcheck_enabled;
extern char *healthcheck_filename;

struct HCEntry;
struct VCEntry;
extern ClassAllocator<HCEntry> hcEntryAllocator;
extern ClassAllocator<VCEntry> vcEntryAllocator;

typedef HashMap<const char *, StringHashFns, HCEntry *> EntryMap;
extern EntryMap entry_map;

void start_read_config_values();
bool read_entry(int fd);
HCEntry* find_entry(const char *hostname);

struct HCEntry : RefCountObj
{
  HCEntry() {}
  virtual ~HCEntry() {}
  bool hc_switch;
  HTTPHdr req_hdr;
  char *hostname;
  int port;
  unsigned int ttl;

  static HCEntry *allocate()
  {
    HCEntry *hc_entry = hcEntryAllocator.alloc();
    hc_entry->init();
    return hc_entry;
  }
  virtual void destroy()
  {
    req_hdr.destroy();
    ats_free(hostname);
    hcEntryAllocator.free(this);
  }
  virtual void free()
  {
    destroy();
  }
private:
  HCEntry(const HCEntry &hc_entry);
  HCEntry& operator=(const HCEntry &hc_entry);

  void init();
};

struct VCEntry
{
  VCEntry() {}
  virtual ~VCEntry() {}
  VConnection *vc;
  MIOBuffer *read_buffer;
  MIOBuffer *write_buffer;
  VIO *read_vio;
  VIO *write_vio;

  static VCEntry *allocate()
  {
    VCEntry *vc_entry = vcEntryAllocator.alloc();
    vc_entry->init();
    return vc_entry;
  }
  virtual void clear();
  virtual void destroy()
  {
    clear();
    vcEntryAllocator.free(this);
  }
private:
  VCEntry(const VCEntry &vc_entry);
  VCEntry& operator=(const VCEntry &vc_entry);

  void init();
};

#endif
