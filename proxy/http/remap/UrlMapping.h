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

#ifndef _URL_MAPPING_H_
#define _URL_MAPPING_H_

#include "Main.h"
#include "Error.h"
#include "URL.h"
#include "RemapPluginInfo.h"
#include "HttpConfig.h"

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

static const unsigned int MAX_REMAP_PLUGIN_CHAIN = 10;

#define MAX_ACL_CHECKLIST_COUNT   2

#define REGEX_TYPE_NONE  0
#define REGEX_TYPE_HOST  1
#define REGEX_TYPE_PATH  2
#define REGEX_TYPE_FULL  4

/**
 *
**/
class redirect_tag_str
{
public:
   redirect_tag_str()
     : next(0), chunk_str(NULL), type(0)
    { }

  ~redirect_tag_str()
  {
    type = 0;
    if (chunk_str) {
      ats_free(chunk_str);
      chunk_str = NULL;
    }
  }

  redirect_tag_str *next;
  char *chunk_str;
  char type;                    /* s - string, r - referer, t - url_to, f - url_from, o - origin url */
  static redirect_tag_str *parse_format_redirect_url(char *url);
};

struct ACLContext;
class ACLMethodIpCheckList;
class ACLRefererCheckList;

class CacheControlConfig
{
  public:
    CacheControlConfig() : revalidate_after(-1),
    pin_in_cache_for(-1),
    ttl_in_cache(-1),
    never_cache(false)
  {
  }

  public:
    // Data for external use
    //
    //   Describes the cache-control for a specific URL
    //
    int revalidate_after;
    int pin_in_cache_for;
    int ttl_in_cache;
    bool never_cache;
};

class ClientControlStat
{
  public:
    ClientControlStat() : _active_connections(0), _current_resp_bps(0),
    _total_resp_bytes(0), _last_resp_bytes(0) {
      _last_calc_time = ink_get_hrtime();
      ink_mutex_init(&_lock, "ClientControlStat");
    }

    ~ClientControlStat() {
      ink_mutex_destroy(&_lock);
    }

    inline int getActiveConnections() {
      return _active_connections;
    }

    inline void incActiveConnections() {
      ink_atomic_increment(&_active_connections, 1);
      //fprintf(stderr, "inc: %d\n", ink_atomic_increment(&_active_connections, 1) + 1);
    }

    inline void decActiveConnections() {
      ink_atomic_increment(&_active_connections, -1);
      //fprintf(stderr, "inc: %d\n", ink_atomic_increment(&_active_connections, -1) - 1);
    }

    inline int getCurrentBps() {
      return _current_resp_bps;
    }

    void incResponseBytes(const int64_t bytes);

  private:
    ink_mutex _lock;
    volatile int _active_connections;
    volatile int64_t _current_resp_bps;
    volatile int64_t _total_resp_bytes;
    int64_t _last_resp_bytes;
    int64_t _last_calc_time; //last time to calculate BPS
};

extern int cluster_active_machine_count;

/**
 * Used to store the mapping for class UrlRewrite
**/
class url_mapping
{
public:
  url_mapping(int rank = 0);
  ~url_mapping();

  bool add_plugin(remap_plugin_info *i, void* ih);
  remap_plugin_info *get_plugin(unsigned int) const;

  inline void* get_instance(unsigned int index) const { return _instance_data[index]; };
  void delete_instance(unsigned int index);
  void Print();

  inline remap_plugin_info **get_plugins() {
    return _plugin_list;
  }

  inline int getRank() const {
    return _rank;
  }

  inline bool needCheckMethodIp() const {
    return _aclMethodIpCheckListCount > 0;
  }

  inline bool needCheckReferer() const {
    return _aclRefererCheckListCount > 0;
  }

  inline bool needCheckRefererHost() const {
    return _needCheckRefererHost;
  }

  inline bool needConvertPath() const {
    return !(isFullRegex() || ((regex_type & REGEX_TYPE_PATH)
          == REGEX_TYPE_PATH));
  }

  inline bool isFullRegex() const {
    return ((regex_type & REGEX_TYPE_FULL) == REGEX_TYPE_FULL);
  }

  int checkMethodIp(const ACLContext & context);
  int checkReferer(const ACLContext & context);

  int setMethodIpCheckLists(ACLMethodIpCheckList **checkLists,
      const int count);

  int setRefererCheckLists(ACLRefererCheckList **checkLists,
      const int count);

  void setFromUrl(const char *url_str, const int url_len);

  void setToUrl(const char *url_str, const int url_len);

  inline const char *getRawFromUrl(int *length) {
    *length = _rawFromUrlLen;
    return _rawFromUrlStr;
  }

  inline const char *getRawToUrl(int *length) {
    *length = _rawToUrlLen;
    return _rawToUrlStr;
  }

  inline void setRawFromUrl(const char *url_str, const int url_len) {
    ats_free(_rawFromUrlStr);
    _rawFromUrlLen = url_len;
    _rawFromUrlStr = (char *)ats_malloc(url_len + 1);
    memcpy(_rawFromUrlStr, url_str, url_len);
    *(_rawFromUrlStr + url_len) = '\0';
  }

  inline void setRawToUrl(const char *url_str, const int url_len) {
    ats_free(_rawToUrlStr);
    _rawToUrlLen = url_len;
    _rawToUrlStr = (char *)ats_malloc(url_len + 1);
    memcpy(_rawToUrlStr, url_str, url_len);
    *(_rawToUrlStr + url_len) = '\0';
  }

  inline void incActiveConnections() {
    if (overridableHttpConfig == NULL) {
      return;
    }
    if (overridableHttpConfig->max_active_client_connections <= 0) {
      return;
    }

    clientControlStat.incActiveConnections();
  }

  inline void decActiveConnections() {
    if (overridableHttpConfig == NULL) {
      return;
    }
    if (overridableHttpConfig->max_active_client_connections <= 0) {
      return;
    }

    clientControlStat.decActiveConnections();
  }

  inline void incResponseBytes(const int64_t bytes) {
    if (overridableHttpConfig == NULL) {
      return;
    }
    if (overridableHttpConfig->max_bandwidth <= 0) {
      return;
    }

    clientControlStat.incResponseBytes(bytes);
  }

  inline bool clientConnectionExceed() {
    if (overridableHttpConfig == NULL) {
      return false;
    }
    if (overridableHttpConfig->max_active_client_connections <= 0) {
      return false;
    }

    return clientControlStat.getActiveConnections() >=
      overridableHttpConfig->max_active_client_connections;
  }

  inline bool clientBandWidthExceed() {
    if (overridableHttpConfig == NULL) {
      return false;
    }
    if (overridableHttpConfig->max_bandwidth <= 0) {
      return false;
    }

    return clientControlStat.getCurrentBps() >
      overridableHttpConfig->max_bandwidth / cluster_active_machine_count;
  }

  URL fromURL;
  URL toUrl; // Default TO-URL (from remap.config)
  bool homePageRedirect;
  bool unique;                  // INKqa11970 - unique mapping
  bool default_redirect_url;
  bool wildcard_from_scheme;    // from url is '/foo', only http or https for now
  char *filter_redirect_url;    // redirect url when referer filtering enabled
  redirect_tag_str *redir_chunk_list;
  unsigned int plugin_count;
  unsigned int cache_url_convert_plugin_count;
  int regex_type;
  ClientControlStat clientControlStat;
  LINK(url_mapping, link); // For use with the main Queue linked list holding all the mapping
  OverridableHttpConfigParams *overridableHttpConfig;
  CacheControlConfig *cacheControlConfig;

private:
  int urlWhack(char *toWhack, const int url_len);

  bool _needCheckRefererHost;
  int _aclMethodIpCheckListCount;
  int _aclRefererCheckListCount;
  ACLMethodIpCheckList *_aclMethodIpCheckLists[MAX_ACL_CHECKLIST_COUNT];
  ACLRefererCheckList *_aclRefererCheckLists[MAX_ACL_CHECKLIST_COUNT];

  remap_plugin_info* _plugin_list[MAX_REMAP_PLUGIN_CHAIN];
  void* _instance_data[MAX_REMAP_PLUGIN_CHAIN];
  char *_rawFromUrlStr;
  char *_rawToUrlStr;
  int _rawFromUrlLen;
  int _rawToUrlLen;
  int _rank;
};

class UrlMappingContainer {
public:
 UrlMappingContainer()
   : _mapping(NULL), _toURLPtr(NULL), _heap(NULL)
    { }

  UrlMappingContainer(HdrHeap *heap)
    : _mapping(NULL), _toURLPtr(NULL), _heap(heap)
  { }


  ~UrlMappingContainer() {
    deleteToURL();
  }

  inline URL *getToURL() const {
    return _toURLPtr;
  }

  inline url_mapping *getMapping() const {
    return _mapping;
  }

  void set(url_mapping *m) {
    deleteToURL();
    _mapping = m;
    _toURLPtr = m ? &(m->toUrl) : NULL;
  }

  void set(HdrHeap *heap) {
    _heap = heap;
  }

  URL *createNewToURL() {
    ink_assert(_heap != NULL);
    deleteToURL();
    _toURL.create(_heap);
    _toURLPtr = &_toURL;
    return _toURLPtr;
  }

  void deleteToURL() {
    if (_toURLPtr == &_toURL) {
      _toURL.clear();
    }
  }

  void clear() {
    deleteToURL();
    _mapping = NULL;
    _toURLPtr = NULL;
    _heap = NULL;
  }

private:
  url_mapping *_mapping;
  URL *_toURLPtr;
  URL _toURL;
  HdrHeap *_heap;

  // non-copyable, non-assignable
  UrlMappingContainer(const UrlMappingContainer &orig);
  UrlMappingContainer &operator =(const UrlMappingContainer &rhs);
};

#endif
