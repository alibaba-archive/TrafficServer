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


#ifndef _I_HOT_URLS_H_
#define _I_HOT_URLS_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "libts.h"
#include "P_EventSystem.h"
#include "P_Net.h"
#include "HTTP.h"
#include "HttpSM.h"

#define MAX_URL_SIZE 2048

struct UrlEntry {
  char url[MAX_URL_SIZE];
  int length;
  
  inline bool equals(const char *str, const int len) {
    return length == len && memcmp(url, str, len) == 0;
  }

  inline bool equals(const UrlEntry *entry) {
    return equals(entry->url, entry->length);
  }
};

#endif

