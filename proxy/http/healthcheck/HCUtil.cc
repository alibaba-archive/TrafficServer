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

#include <string.h>
#include "HCUtil.h"

static const int MAX_SIZE = 1024;
static const char* ITEM_DELIM = ";";
static const char* TOKEN_DELIM = "=";
static const char* IGNORE_DELIM = "#";
static const char* NAME = "hostname";
static const int  NAME_LEN = strlen(NAME);
static const char* PORT = "port";
static const int  PORT_LEN = strlen(PORT);
static const char* TTL = "interval";
static const int  TTL_LEN = strlen(TTL);
static const char* REQUEST_PATH = "request_path";
static const int REQUEST_PATH_LEN = strlen(REQUEST_PATH);
static const char* REQUEST_METHOD = "request_method";
static const int REQUEST_METHOD_LEN = strlen(REQUEST_METHOD);
static const char* REQUEST_HOST = "request_host";
static const int REQUEST_HOST_LEN = strlen(REQUEST_HOST);
static const char* DEFAULT_REQUEST_PATH = "/status.html";
static const char* DEFAULT_REQUEST_METHOD = "GET";

int32_t healthcheck_default_ttl = 0;
int32_t healthcheck_serve_stale_but_revalidate = 0;
int healthcheck_enabled = false;
char *healthcheck_filename = NULL;
ClassAllocator<HCEntry> hcEntryAllocator("HCEntryAllocator");
ClassAllocator<VCEntry> vcEntryAllocator("VCEntryAllocator");
EntryMap entry_map;

void
HCEntry::init()
{
  hc_switch = false;
  hostname = NULL;
#define DEFAULT_PORT 80
  port = DEFAULT_PORT;
  ttl = healthcheck_default_ttl;
}

void
VCEntry::clear()
{
  vc = NULL;
  if (read_buffer) {
    free_MIOBuffer(read_buffer);
    read_buffer = NULL;
  }
  if (write_buffer) {
    free_MIOBuffer(write_buffer);
    write_buffer = NULL;
  }
  read_vio = NULL;
  write_vio = NULL;
}

void
VCEntry::init()
{
  vc = NULL;
  read_buffer = NULL;
  write_buffer = NULL;
  read_vio = NULL;
  write_vio = NULL;
}

void
start_read_config_values()
{
  IOCORE_EstablishStaticConfigInt32(healthcheck_default_ttl, "proxy.config.http.healthcheck.default_interval");
  IOCORE_EstablishStaticConfigInt32(healthcheck_serve_stale_but_revalidate, "proxy.config.http.healthcheck.serve_stale_for");

  IOCORE_ReadConfigInt32(healthcheck_enabled, "proxy.config.http.healthcheck.enabled");
  IOCORE_ReadConfigStringAlloc(healthcheck_filename, "proxy.config.http.healthcheck.filename");
}


bool
read_entry(int fd)
{
  char line[MAX_SIZE];
  char *saveItem, *saveToken, *saveIgnore;
  char *str, *item, *token, *value;
  int len;

  const char *request_path = NULL, *request_method = NULL, *request_host = NULL;
  char *key_hostname = NULL;
  int ret = 0;
  HCEntry *entry = NULL;
  while ((ret = ink_file_fd_readline(fd, sizeof(line), line)) > 0) {
    if ('\r' == *line || '\n' == *line || '#' == *line || ' ' == *line || '\t' == *line)
      continue;
    len = strlen(line);
    line[len - 1] = '\0';

    char *str_line = strtok_r(line, IGNORE_DELIM, &saveIgnore);
    for (str = str_line; ; str = NULL) {
      item = strtok_r(str, ITEM_DELIM, &saveItem);
      if (NULL == item)
        break;
      token = strtok_r(item, TOKEN_DELIM, &saveToken);
      value = strtok_r(NULL, TOKEN_DELIM, &saveToken);
      if (NULL == value) {
        entry->destroy();
        return false;
      }
      if (0 == strncmp(NAME, token, NAME_LEN)) {
        if (entry == NULL) {
          entry = HCEntry::allocate();
        } else {
          char req_buf[MAX_SIZE];
          sprintf(req_buf, "%s %s HTTP/1.1\r\nHost: %s\r\nKeep-Alive: 9999\r\nConnection: keep-alive\r\n\r\n",
              request_method ? request_method : DEFAULT_REQUEST_METHOD,
              request_path ? request_path : DEFAULT_REQUEST_PATH,
              request_host ? request_host : entry->hostname);
          const char *a_url = req_buf;
          Debug("healthcheck", "construct url: \n%s", a_url);
          HTTPParser http_parser;
          http_parser_init(&http_parser);
          entry->req_hdr.create(HTTP_TYPE_REQUEST);
          int state = entry->req_hdr.parse_req(&http_parser, &a_url, a_url + strlen(a_url), false);
          if (PARSE_DONE != state &&  PARSE_OK != state) {
            http_parser_clear(&http_parser);
            entry->destroy();
            return false;
          }
          http_parser_clear(&http_parser);
          entry_map.put(key_hostname, entry);

          key_hostname = NULL;
          request_path = NULL;
          request_method = NULL;
          request_host = NULL;
          entry = HCEntry::allocate();
        }
        key_hostname = ats_strdup(value);
        entry->hostname = key_hostname;
        entry->hc_switch = true;
      } else if (0 == strncmp(PORT, token, PORT_LEN)) {
        entry->port = ink_atoi(value);
      } else if (0 == strncmp(TTL, token, TTL_LEN)) {
        entry->ttl = ink_atoi(value);
      } else if (0 == strncmp(REQUEST_PATH, token, REQUEST_PATH_LEN)) {
        request_path = value;
      } else if (0 == strncmp(REQUEST_METHOD, token, REQUEST_METHOD_LEN)) {
        request_method = value;
      } else if (0 == strncmp(REQUEST_HOST, token, REQUEST_HOST_LEN)) {
        request_host = value;
      }
    }
  }
  if (ret < 0) {
    entry->destroy();
    return false;
  } else {
    if (NULL != entry) {
      char req_buf[MAX_SIZE];
      sprintf(req_buf, "%s %s HTTP/1.1\r\nHost: %s\r\nKeep-Alive: 9999\r\nConnection: keep-alive\r\n\r\n",
          request_method ? request_method : DEFAULT_REQUEST_METHOD,
          request_path ? request_path : DEFAULT_REQUEST_PATH,
          request_host ? request_host : entry->hostname);
      const char *a_url = req_buf;
      Debug("healthcheck", "construct url: \n%s", a_url);
      HTTPParser http_parser;
      http_parser_init(&http_parser);
      entry->req_hdr.create(HTTP_TYPE_REQUEST);
      int state = entry->req_hdr.parse_req(&http_parser, &a_url, a_url + strlen(a_url), false);
      if (PARSE_DONE != state &&  PARSE_OK != state) {
        http_parser_clear(&http_parser);
        entry->destroy();
        return false;
      }
      http_parser_clear(&http_parser);
      entry_map.put(key_hostname, entry);
    }
    return true;
  }
}

HCEntry*
find_entry(const char *hostname)
{
  HCEntry *iter = entry_map.get(hostname);
  return iter;
}
