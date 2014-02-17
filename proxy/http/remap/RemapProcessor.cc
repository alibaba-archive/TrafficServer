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

#include "MappingTypes.h"
#include "RemapProcessor.h"

RemapProcessor remapProcessor;
extern ClassAllocator<RemapPlugins> pluginAllocator;

int
RemapProcessor::start(int num_threads, size_t stacksize)
{
  if (_use_separate_remap_thread)
    ET_REMAP = eventProcessor.spawn_event_threads(num_threads, "ET_REMAP", stacksize);  // ET_REMAP is a class member

  return 0;
}

/**
  Most of this comes from UrlRewrite::Remap(). Generally, all this does
  is set "map" to the appropriate entry from the global rewrite_table
  such that we will then have access to the correct url_mapping inside
  perform_remap.

*/
bool
RemapProcessor::setup_for_remap(HttpTransact::State *s)
{
  Debug("url_rewrite", "setting up for remap: %p", s);
  URL *request_url = NULL;
  bool mapping_found = false;
  HTTPHdr *request_header = &s->hdr_info.client_request;
  const char *request_host;
  int request_host_len;
  int request_port;
  bool proxy_request = false;

  s->reverse_proxy = rewrite_table->reverse_proxy;
  s->url_map.set(s->hdr_info.client_request.m_heap);

  if (unlikely((rewrite_table->num_rules_forward == 0) &&
               (rewrite_table->num_rules_forward_with_recv_port == 0))) {
    ink_assert(rewrite_table->forward_mappings.empty() &&
               rewrite_table->forward_mappings_with_recv_port.empty());
    Debug("url_rewrite", "[lookup] No forward mappings found; Skipping...");
    return false;
  }

  // Since we are called before request validity checking
  // occurs, make sure that we have both a valid request
  // header and a valid URL
  if (unlikely(!request_header || (request_url = request_header->url_get()) == NULL || !request_url->valid())) {
    Error("NULL or invalid request data");
    return false;
  }

  request_host = request_header->host_get(&request_host_len);
  request_port = request_header->port_get();
  proxy_request = request_header->is_target_in_url() || ! s->reverse_proxy;

  // Default to empty host.
  if (!request_host) {
    request_host = "";
    request_host_len = 0;
  }

  Debug("url_rewrite", "[lookup] attempting %s lookup", proxy_request ? "proxy" : "normal");

  if (rewrite_table->num_rules_forward_with_recv_port) {
    Debug("url_rewrite", "[lookup] forward mappings with recv port found; Using recv port %d",
          ats_ip_port_host_order(&s->client_info.addr));
    if (rewrite_table->forwardMappingWithRecvPortLookup(request_url, ats_ip_port_host_order(&s->client_info.addr),
                                                         request_host, request_host_len, s->url_map)) {
      Debug("url_rewrite", "Found forward mapping with recv port");
      mapping_found = true;
    } else if (rewrite_table->num_rules_forward == 0) {
      ink_assert(rewrite_table->forward_mappings.empty());
      Debug("url_rewrite", "No forward mappings left");
      return false;
    }
  }

  if (!mapping_found) {
    mapping_found = rewrite_table->forwardMappingLookup(request_url, request_port, request_host, request_host_len, s->url_map);
  }

  if (!proxy_request) { // do extra checks on a server request

    // Save this information for later
    // @amc: why is this done only for requests without a host in the URL?
    s->hh_info.host_len = request_host_len;
    s->hh_info.request_host = request_host;
    s->hh_info.request_port = request_port;

    // If no rules match and we have a host, check empty host rules since
    // they function as default rules for server requests.
    // If there's no host, we've already done this.
    if (!mapping_found && rewrite_table->nohost_rules && request_host_len) {
      Debug("url_rewrite", "[lookup] nothing matched");
      mapping_found = rewrite_table->forwardMappingLookup(request_url, 0, "", 0, s->url_map);
    }

    if (mapping_found) {
      // Downstream mapping logic (e.g., self::finish_remap())
      // apparently assumes the presence of the target in the URL, so
      // we need to copy it. Perhaps it's because it's simpler to just
      // do the remap on the URL and then fix the field at the end.
      request_header->set_url_target_from_host_field();
    }
  }

  if (mapping_found) {
    request_header->mark_target_dirty();

    url_mapping *map = s->url_map.getMapping();
    if ((map != NULL) && (map->overridableHttpConfig != NULL) &&
        (s->txn_conf != &s->my_txn_conf))
    {
      s->txn_conf = map->overridableHttpConfig;
      Debug("url_rewrite", "use mapping's overridableHttpConfig");
    }

    if (map->cacheControlConfig != NULL) {
      s->cacheControlByRemap = true;
      s->cache_control.revalidate_after = map->cacheControlConfig->revalidate_after;
      s->cache_control.pin_in_cache_for = map->cacheControlConfig->pin_in_cache_for;
      s->cache_control.ttl_in_cache = map->cacheControlConfig->ttl_in_cache;
      s->cache_control.never_cache = map->cacheControlConfig->never_cache;

      Debug("url_rewrite", "cache control use mapping's config");
    }
  } else {
    Debug("url_rewrite", "RemapProcessor::setup_for_remap did not find a mapping");
  }

  return mapping_found;
}

bool
RemapProcessor::finish_remap(HttpTransact::State *s)
{
  url_mapping *map = NULL;
  HTTPHdr *request_header = &s->hdr_info.client_request;
  URL *request_url = request_header->url_get();
  char **redirect_url = &s->remap_redirect;
  char host_hdr_buf[TS_MAX_HOST_NAME_LEN], tmp_referer_buf[4096], tmp_redirect_buf[4096], tmp_buf[2048], *c;
  const char *remapped_host;
  int remapped_host_len, remapped_port, tmp;
  int from_len;
  bool remap_found = false;

  map = s->url_map.getMapping();
  if (!map) {
    return false;
  }

  // Do fast ACL filtering (it is safe to check map here)
  if (rewrite_table->PerformACLFiltering(s, map) == ACL_ACTION_DENY_INT) {
    return false;
  }

  // Check referer filtering rules
  if (map->needCheckReferer()) {
    const char *referer_str;
    ACLContext aclContext;

    if (request_header->presence(MIME_PRESENCE_REFERER) &&
        (referer_str=request_header->
         value_get(MIME_FIELD_REFERER, MIME_LEN_REFERER,
           &aclContext.refererUrl.length)) != NULL) {
      if (aclContext.refererUrl.length >= (int)sizeof(tmp_referer_buf)) {
        aclContext.refererUrl.length = sizeof(tmp_referer_buf) - 1;
      }

      if (aclContext.refererUrl.length > 0) {
        memcpy(tmp_referer_buf, referer_str, aclContext.refererUrl.length);
        *(tmp_referer_buf + aclContext.refererUrl.length) = '\0';

        if (map->needCheckRefererHost() && aclContext.refererUrl.length > 7) {
          do {
            if (strncasecmp(tmp_referer_buf, "http://", 7) == 0) {
              aclContext.refererHostname.str = tmp_referer_buf + 7;
            }
            else if (strncasecmp(tmp_referer_buf, "https://", 8) == 0) {
              aclContext.refererHostname.str = tmp_referer_buf + 8;
            }
            else {
              break;
            }

            const char *hostname_end = (const char *)memchr(
                aclContext.refererHostname.str, '/',
                (tmp_referer_buf + aclContext.refererUrl.length) -
                aclContext.refererHostname.str);
            if (hostname_end != NULL) {
              aclContext.refererHostname.length = hostname_end -
                aclContext.refererHostname.str;
            }
            else {
              aclContext.refererHostname.length = (tmp_referer_buf +
                  aclContext.refererUrl.length) - aclContext.refererHostname.str;
            }

            const char *colon = (const char *)memchr(
                aclContext.refererHostname.str, ':',
                aclContext.refererHostname.length);
            if (colon != NULL) {
              aclContext.refererHostname.length = colon -
                aclContext.refererHostname.str;
            }

            /*
            Debug("url_rewrite", "referer host: %.*s(%d)",
                aclContext.refererHostname.length, aclContext.refererHostname.str,
                aclContext.refererHostname.length);
            */
          } while (0);
        }
      }
      else {
        *tmp_referer_buf = '\0';
      }
    }
    else {
      *tmp_referer_buf = '\0';
    }
    aclContext.refererUrl.str = tmp_referer_buf;

    if (map->checkReferer(aclContext) == ACL_ACTION_DENY_INT) {
      Debug("url_rewrite", "ACL referer check denied, referer url: %.*s",
        aclContext.refererUrl.length, aclContext.refererUrl.str);

      if (!map->default_redirect_url) {
        if ((s->filter_mask & URL_REMAP_FILTER_REDIRECT_FMT) != 0 && map->redir_chunk_list) {
          redirect_tag_str *rc;
          int len;

          len = 0;
          *tmp_redirect_buf = '\0';
          for (rc = map->redir_chunk_list; rc; rc = rc->next) {
            switch (rc->type) {
              case 's':
                c = rc->chunk_str;
                break;
              case 'r':
                c = (aclContext.refererUrl.length > 0) ? tmp_referer_buf : NULL;
                break;
              case 'f':
              case 't':
                remapped_host = (rc->type == 'f') ?
                  map->fromURL.string_get_buf(tmp_buf, (int)sizeof(tmp_buf), &from_len) :
                  ((s->url_map).getToURL())->string_get_buf(tmp_buf,
                  (int)sizeof(tmp_buf), &from_len);
                if (remapped_host && from_len > 0) {
                  c = tmp_buf;
                }
                else {
                  c = NULL;
                }
                break;
              case 'o':
                c = s->pristine_url.string_get_ref(NULL);
                break;
              default:
                c = NULL;
                break;
            };

            if (c != NULL && len < (int)(sizeof(tmp_redirect_buf) - 1)) {
              len += snprintf(tmp_redirect_buf + len,
                  sizeof(tmp_redirect_buf) - len, "%s", c);
            }
          }

          if (*redirect_url != NULL) {
            ats_free(*redirect_url);
          }
          *redirect_url = ats_strdup(tmp_redirect_buf);
        }
      } else {
        if (*redirect_url != NULL) {
          ats_free(*redirect_url);
        }
        *redirect_url = ats_strdup(rewrite_table->http_default_redirect_url);
      }

      if (*redirect_url == NULL) {
        *redirect_url = ats_strdup(map->filter_redirect_url ? map->filter_redirect_url :
                                   rewrite_table->http_default_redirect_url);
      }

      return false;
    }
  }

  remap_found = true;

  // We also need to rewrite the "Host:" header if it exists and
  //   pristine host hdr is not enabled
  int host_len;
  const char *host_hdr = request_header->value_get(MIME_FIELD_HOST, MIME_LEN_HOST, &host_len);

  if (request_url && host_hdr != NULL && s->txn_conf->maintain_pristine_host_hdr == 0) {
    // Debug code to print out old host header.  This was easier before
    //  the header conversion.  Now we have to copy to gain null
    //  termination for the Debug() call
    if (is_debug_tag_set("url_rewrite")) {
      int old_host_hdr_len;
      char *old_host_hdr = (char *) request_header->value_get(MIME_FIELD_HOST, MIME_LEN_HOST, &old_host_hdr_len);

      if (old_host_hdr) {
        old_host_hdr = ats_strndup(old_host_hdr, old_host_hdr_len);
        Debug("url_rewrite", "Host: Header before rewrite %.*s", old_host_hdr_len, old_host_hdr);
        ats_free(old_host_hdr);
      }
    }
    //
    // Create the new host header field being careful that our
    //   temporary buffer has adequate length
    //
    remapped_host = request_url->host_get(&remapped_host_len);
    remapped_port = request_url->port_get_raw();

    if (TS_MAX_HOST_NAME_LEN > remapped_host_len) {
      tmp = remapped_host_len;
      memcpy(host_hdr_buf, remapped_host, remapped_host_len);
      if (remapped_port) {
        tmp += snprintf(host_hdr_buf + remapped_host_len, TS_MAX_HOST_NAME_LEN - remapped_host_len - 1,
                        ":%d", remapped_port);
    }
    } else {
      tmp = TS_MAX_HOST_NAME_LEN;
    }

    // It is possible that the hostname is too long.  If it is punt,
    //   and remove the host header.  If it is too long the HostDB
    //   won't be able to resolve it and the request will not go
    //   through
    if (tmp >= TS_MAX_HOST_NAME_LEN) {
      request_header->field_delete(MIME_FIELD_HOST, MIME_LEN_HOST);
      Debug("url_rewrite", "Host: Header too long after rewrite");
    } else {
      Debug("url_rewrite", "Host: Header after rewrite %.*s", tmp, host_hdr_buf);
      request_header->value_set(MIME_FIELD_HOST, MIME_LEN_HOST, host_hdr_buf, tmp);
    }
  }

  return remap_found;
}

Action *
RemapProcessor::perform_remap(Continuation *cont, HttpTransact::State *s)
{
  Debug("url_rewrite", "Beginning RemapProcessor::perform_remap");
  HTTPHdr *request_header = &s->hdr_info.client_request;
  URL *request_url = request_header->url_get();
  url_mapping *map = s->url_map.getMapping();
  host_hdr_info *hh_info = &(s->hh_info);

  if (!map) {
    Error("Could not find corresponding url_mapping for this transaction %p", s);
    Debug("url_rewrite", "Could not find corresponding url_mapping for this transaction");
    ink_assert(!"this should never happen -- call setup_for_remap first");
    cont->handleEvent(EVENT_REMAP_ERROR, NULL);
    return ACTION_RESULT_DONE;
  }

  if (_use_separate_remap_thread) {
    RemapPlugins *plugins = pluginAllocator.alloc();

    plugins->setState(s);
    plugins->setRequestUrl(request_url);
    plugins->setRequestHeader(request_header);
    plugins->setHostHeaderInfo(hh_info);

    // Execute "inline" if not using separate remap threads.
    ink_assert(cont->mutex->thread_holding == this_ethread());
    plugins->mutex = cont->mutex;
    plugins->action = cont;
    SET_CONTINUATION_HANDLER(plugins, &RemapPlugins::run_remap);
    eventProcessor.schedule_imm(plugins, ET_REMAP);

    return &plugins->action;
  } else {
    RemapPlugins plugins(s, request_url, request_header, hh_info);
    int ret = 0;

    do {
      ret = plugins.run_single_remap();
    } while (ret == 0);

    return ACTION_RESULT_DONE;
  }
}

bool RemapProcessor::findMapping(URL *request_url, UrlMappingContainer &url_map)
{
  bool mapping_found = false;

  if (unlikely(rewrite_table->num_rules_forward == 0)) {
    return false;
  }

  const char *host;
  int host_len;
  host = request_url->host_get(&host_len);
  if (host_len == 0) {
    return false;
  }

  mapping_found = rewrite_table->forwardMappingLookup(request_url, request_url->port_get_raw(),
      host, host_len, url_map);

  if (!mapping_found && rewrite_table->reverse_proxy) { // do extra checks on a server request
    // If no rules match and we have a host, check empty host rules since
    // they function as default rules for server requests.
    // If there's no host, we've already done this.
    if (rewrite_table->nohost_rules && host_len > 0) {
      mapping_found = rewrite_table->forwardMappingLookup(request_url, 0, "", 0, url_map);
    }
  }

  return mapping_found;
}

struct URLPartsBuffer {
  char scheme[8];  //http or https etc
  char host[256];
  char path[1024];  //NOT start with /
  char query[4096]; //url parameters such as: key=value&login=foo
};

void RemapProcessor::bindUrlBuffer(struct URLPartsBuffer *urlBuffer, RemapUrlInfo *urlInfo)
{
  urlInfo->scheme.str = urlBuffer->scheme;
  urlInfo->scheme.size = sizeof(urlBuffer->scheme);
  urlInfo->scheme.length = 0;

  urlInfo->host.str = urlBuffer->host;
  urlInfo->host.size = sizeof(urlBuffer->host);
  urlInfo->host.length = 0;

  urlInfo->path.str = urlBuffer->path;
  urlInfo->path.size = sizeof(urlBuffer->path);
  urlInfo->path.length = 0;

  urlInfo->query.str = urlBuffer->query;
  urlInfo->query.size = sizeof(urlBuffer->query);
  urlInfo->query.length = 0;
}

bool RemapProcessor::copyFromUrl(URL *srcUrl, RemapUrlInfo *destUrl)
{
  const char *value;
  int length;

  value = srcUrl->scheme_get(&length);
  if (length >= destUrl->scheme.size) {
    return false;
  }
  memcpy(destUrl->scheme.str, value, length);
  destUrl->scheme.length = length;

  value = srcUrl->host_get(&length);
  if (length >= destUrl->host.size) {
    return false;
  }
  memcpy(destUrl->host.str, value, length);
  destUrl->host.length = length;

  value = srcUrl->path_get(&length);
  if (length >= destUrl->path.size) {
    return false;
  }
  if (length > 0) {
    memcpy(destUrl->path.str, value, length);
  }
  destUrl->path.length = length;

  value = srcUrl->query_get(&length);
  if (length >= destUrl->query.size) {
    return false;
  }
  if (length > 0) {
    memcpy(destUrl->query.str, value, length);
  }
  destUrl->query.length = length;

  destUrl->port = srcUrl->port_get_raw();
  return true;
}

bool RemapProcessor::convert_cache_url(const char *in_url, const int in_url_len,
      char *out_url, const int out_url_size, int *out_url_len, int *flags)
{
  bool found;
  bool maintain_pristine_host_hdr;
  URL *newURL;
  HdrHeap *hdrHeap = new_HdrHeap();
  UrlMappingContainer url_map(hdrHeap);
  URL old_url;
  int tmp_flags;

  *flags = 0;
  old_url.create(hdrHeap);
  do {
    if (old_url.parse(in_url, in_url_len) == PARSE_ERROR) {
      found = false;
      break;
    }

    found = findMapping(&old_url, url_map);
    if (!found) {
      break;
    }

    url_mapping *mapping = url_map.getMapping();
    if (mapping->overridableHttpConfig == NULL) {
      maintain_pristine_host_hdr = HttpConfig::m_master.oride.maintain_pristine_host_hdr;
    }
    else {
      maintain_pristine_host_hdr = mapping->overridableHttpConfig->maintain_pristine_host_hdr;
    }

    if (mapping->overridableHttpConfig) {
      if (mapping->overridableHttpConfig->cache_cluster_cache_local) {
        *flags = URL_CONVERT_FLAG_CLUSTER_CACHE_LOCAL;
      }
    }
    else if (CacheProcessor::IsCacheClustering()) {
      HttpConfigParams *httpConfig = HttpConfig::acquire();
      if (httpConfig != NULL) {
        if (httpConfig->oride.cache_cluster_cache_local) {
          *flags = URL_CONVERT_FLAG_CLUSTER_CACHE_LOCAL;
        }
        HttpConfig::release(httpConfig);
      }
    }

    newURL = &old_url;
    rewrite_table->doRemap(url_map, newURL, maintain_pristine_host_hdr);
    if (mapping->cache_url_convert_plugin_count == 0) {
      newURL->string_get_buf(out_url, out_url_size, out_url_len);
      if (*out_url_len == out_url_size) {
        *out_url_len = out_url_size - 1;
      }
      *(out_url + *out_url_len) = '\0';
      break;
    }

    URLPartsBuffer parts;
    RemapUrlInfo url;
    RemapUrlInfo *inURL = &url;
    TSRemapStatus status;

    bindUrlBuffer(&parts, inURL);
    if (!copyFromUrl(newURL, inURL)) {
      found = false;
      break;
    }

    remap_plugin_info **plugins = mapping->get_plugins();
    for (unsigned int i=0; i<mapping->plugin_count; i++) {
       if (plugins[i]->fp_tsremap_convert_cache_url != NULL) {
         tmp_flags = 0;
         status = plugins[i]->fp_tsremap_convert_cache_url(mapping->get_instance(i), inURL, &tmp_flags);
         if (status != TSREMAP_ERROR && tmp_flags != 0) {
           *flags |= tmp_flags;
         }
         if (status == TSREMAP_NO_REMAP_STOP || status == TSREMAP_DID_REMAP_STOP || status == TSREMAP_ERROR) {
           break;
         }
       }
    }

    char port_str[16];
    if (inURL->port > 0) {
      int default_port;
      if (inURL->scheme.length == 4 && memcmp(inURL->scheme.str, "http", 4) == 0) {
        default_port = 80;
      }
      else if (inURL->scheme.length == 5 && memcmp(inURL->scheme.str, "https", 5) == 0) {
        default_port = 443;
      }
      else {
        default_port = 0;
      }

      if (inURL->port == default_port) {
        *port_str = '\0';
      }
      else {
        snprintf(port_str, sizeof(port_str), ":%d", inURL->port);
      }
    }
    else {
      *port_str = '\0';
    }

    *out_url_len = snprintf(out_url, out_url_size, "%.*s://%.*s%s/%.*s",
        inURL->scheme.length, inURL->scheme.str,
        inURL->host.length, inURL->host.str, port_str,
        inURL->path.length, inURL->path.str);
    if (inURL->query.length > 0) {
      *out_url_len += snprintf(out_url + *out_url_len,
          out_url_size - *out_url_len, "?%.*s",
          inURL->query.length, inURL->query.str);
    }
  } while (0);

  old_url.clear();
  hdrHeap->destroy();

  if (!found) {
    *out_url = '\0';
    *out_url_len = 0;
  }

  return found;
}

