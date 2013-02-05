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

#include "ink_port.h"
#include "ACLMethodIpCheckList.h"
#include "ACLRefererCheckList.h"
#include "UrlMapping.h"
#include "HttpConfig.h"

/**
 *
**/
url_mapping::url_mapping(int rank /* = 0 */)
  : fromURL(), toUrl(), homePageRedirect(false), 
    unique(false), default_redirect_url(false),
    wildcard_from_scheme(false),
    filter_redirect_url(NULL), 
    redir_chunk_list(0), _plugin_count(0), overridableHttpConfig(NULL), 
    _needCheckRefererHost(false), _aclMethodIpCheckListCount(0), 
    _aclRefererCheckListCount(0), _rank(rank)
{
  memset(_plugin_list, 0, sizeof(_plugin_list));
  memset(_instance_data, 0, sizeof(_instance_data));
  memset(_aclMethodIpCheckLists, 0, sizeof(_aclMethodIpCheckLists));
  memset(_aclRefererCheckLists, 0, sizeof(_aclRefererCheckLists));
}


/**
 *
**/
url_mapping::~url_mapping()
{
  redirect_tag_str *rc;

  filter_redirect_url = (char *)ats_free_null(filter_redirect_url);

  while ((rc = redir_chunk_list) != 0) {
    redir_chunk_list = rc->next;
    delete rc;
  }

  // Delete all instance data
  for (unsigned int i = 0; i < _plugin_count; ++i) {
    delete_instance(i);
  }

  // Destroy the URLs
  fromURL.destroy();
  toUrl.destroy();

  if (overridableHttpConfig != NULL) {
    delete overridableHttpConfig;
    overridableHttpConfig = NULL;
  }
}


/**
 *
**/
bool
url_mapping::add_plugin(remap_plugin_info* i, void* ih)
{
  if (_plugin_count >= MAX_REMAP_PLUGIN_CHAIN)
    return false;

  _plugin_list[_plugin_count] = i;
  _instance_data[_plugin_count] = ih;
  ++_plugin_count;

  return true;
}


/**
 *
**/
remap_plugin_info*
url_mapping::get_plugin(unsigned int index) const
{
  Debug("url_rewrite", "get_plugin says we have %d plugins and asking for plugin %d", _plugin_count, index);
  if ((_plugin_count == 0) || unlikely(index > _plugin_count))
    return NULL;

  return _plugin_list[index];
}

int url_mapping::checkMethodIp(const ACLContext & context) {
  int result;
  int action;
  result = ACL_ACTION_NONE_INT;
  for (int i=0; i<_aclMethodIpCheckListCount; i++) {
    action = _aclMethodIpCheckLists[i]->check(context);
    if (action == ACL_ACTION_DENY_INT) {
      return action;
    }
    if (action == ACL_ACTION_ALLOW_INT) {
      result = action;
    }
  }

  return result;
}

int url_mapping::checkReferer(const ACLContext & context) {
  int result;
  int action;
  result = ACL_ACTION_NONE_INT;
  for (int i=0; i<_aclRefererCheckListCount; i++) {
    action = _aclRefererCheckLists[i]->check(context);
    if (action == ACL_ACTION_DENY_INT) {
      return action;
    }
    if (action == ACL_ACTION_ALLOW_INT) {
      result = action;
    }
  }

  return result;
}

int url_mapping::setMethodIpCheckLists(ACLMethodIpCheckList **checkLists, 
    const int count)
{
  if (count < 0) {
    return EINVAL;
  }

  if (count > MAX_ACL_CHECKLIST_COUNT) {
    return ENOSPC;
  }

  _aclMethodIpCheckListCount = count;
  for (int i=0; i<count; i++) {
    _aclMethodIpCheckLists[i] = checkLists[i];
  }

  return 0;
}

int url_mapping::setRefererCheckLists(ACLRefererCheckList **checkLists, 
    const int count)
{
  if (count < 0) {
    return EINVAL;
  }

  if (count > MAX_ACL_CHECKLIST_COUNT) {
    return ENOSPC;
  }

  _needCheckRefererHost = false;
  _aclRefererCheckListCount = count;
  for (int i=0; i<count; i++) {
    _aclRefererCheckLists[i] = checkLists[i];

    if (checkLists[i]->needCheckHost()) {
      _needCheckRefererHost = true;
    }
  }

  return 0;
}

/**
 *
**/
void
url_mapping::delete_instance(unsigned int index)
{
  void *ih = get_instance(index);
  remap_plugin_info* p = get_plugin(index);

  if (ih && p && p->fp_tsremap_delete_instance)
    p->fp_tsremap_delete_instance(ih);
}

void
url_mapping::Print()
{
  char from_url_buf[131072], to_url_buf[131072];

  fromURL.string_get_buf(from_url_buf, (int) sizeof(from_url_buf));
  toUrl.string_get_buf(to_url_buf, (int) sizeof(to_url_buf));
  printf("\t %s %s=> %s %s [plugins %s enabled; running with %u plugins]\n", 
      from_url_buf, unique ? "(unique)" : "", to_url_buf,
      homePageRedirect ? "(R)" : "", _plugin_count > 0 ? "are" : "not", 
      _plugin_count);
}

/**
 *
**/
redirect_tag_str *
redirect_tag_str::parse_format_redirect_url(char *url)
{
  char *c;
  redirect_tag_str *r, **rr;
  redirect_tag_str *list = 0;
  char type = 0;

  if (url && *url) {
    for (rr = &list; *(c = url) != 0;) {
      for (type = 's'; *c; c++) {
        if (c[0] == '%') {
          char tmp_type = (char) tolower((int) c[1]);
          if (tmp_type == 'r' || tmp_type == 'f' || tmp_type == 't' || tmp_type == 'o') {
            if (url == c)
              type = tmp_type;
            break;
          }
        }
      }
      r = NEW(new redirect_tag_str());
      if (likely(r)) {
        if ((r->type = type) == 's') {
          char svd = *c;
          *c = 0;
          r->chunk_str = ats_strdup(url);
          *c = svd;
          url = c;
        } else
          url += 2;
        (*rr = r)->next = 0;
        rr = &(r->next);
        //printf("\t***********'%c' - '%s'*******\n",r->type,r->chunk_str ? r->chunk_str : "<NULL>");
      } else
        break;                  /* memory allocation error */
    }
  }
  return list;
}

