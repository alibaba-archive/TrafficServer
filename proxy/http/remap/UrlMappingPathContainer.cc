#include "UrlMappingPathContainer.h"

UrlMappingPathContainer::UrlMappingPathContainer() :
  _path_trie_min_rank(-1), _regex_min_rank(-1)
{
}

UrlMappingPathContainer::~UrlMappingPathContainer()
{
  UrlMappingRegexMatcher *path_regex;
  while ((path_regex=_path_regex_list.pop()) != NULL) {
    delete path_regex;
  }
}

bool
UrlMappingPathContainer::Insert(url_mapping *mapping)
{
  if (mapping->regex_type & REGEX_TYPE_PATH) {
    if (_regex_min_rank < 0) {
      _regex_min_rank = mapping->getRank();
    }
    UrlMappingRegexMatcher *reg_map = NEW(new UrlMappingRegexMatcher(mapping));
    if (processPathRegexMapping(reg_map)) {
      _path_regex_list.enqueue(reg_map);
      return true;
    }
    else {
      delete reg_map;
      return false;
    }
  }
  else {
    if (_path_trie_min_rank < 0) {
      _path_trie_min_rank = mapping->getRank();
    }
    return _path_tries.Insert(mapping);
  }
}

void
UrlMappingPathContainer::Print()
{
  _path_tries.Print();
  forl_LL(UrlMappingRegexMatcher, list_iter, _path_regex_list) {
    list_iter->getMapping()->Print();
  }
}

url_mapping *
UrlMappingPathContainer::Search(URL *request_url, UrlMappingContainer &mapping_container)
{
  url_mapping *mapping;
  URL *expanded_url;
  const char *request_path;
  int request_path_len;
  char to_path[1024];
  int to_path_len;
  int match_result;
  int rank_ceiling;

  if (mapping_container.getMapping() == NULL) {
    rank_ceiling = INT_MAX;
  }
  else {
    rank_ceiling = mapping_container.getMapping()->getRank();
  }

  mapping = NULL;
  if (_path_trie_min_rank >= 0 && _path_trie_min_rank < rank_ceiling) {
    url_mapping *found = _path_tries.Search(request_url);
    if (found != NULL) {
      if (found->getRank() < rank_ceiling) {
        mapping = found;
        rank_ceiling = mapping->getRank();
        mapping_container.set(mapping);
        expanded_url = mapping_container.createNewToURL();
        expanded_url->copy(&(mapping->toUrl));
      }
    }
  }

  if (_regex_min_rank < 0 || _regex_min_rank > rank_ceiling) { //do not need match
    return mapping;
  }

  request_path = request_url->path_get(&request_path_len);
  forl_LL(UrlMappingRegexMatcher, path_regex, _path_regex_list) {
    if (path_regex->getMapping()->getRank() > rank_ceiling) {
      break;
    }

    if ((match_result=path_regex->match(request_path, request_path_len,
            to_path, sizeof(to_path), &to_path_len)) > 0)
    {
      mapping = path_regex->getMapping();
      mapping_container.set(mapping);
      expanded_url = mapping_container.createNewToURL();
      expanded_url->copy(&(mapping->toUrl));
      expanded_url->path_set(to_path, to_path_len);
      break;
    }
    else if (match_result == PCRE_ERROR_NOMATCH) {
      Debug("url_rewrite_regex", "Request URL path [%.*s] did NOT match regex in mapping of rank %d",
          request_path_len, request_path, path_regex->getMapping()->getRank());
    } else {
      Warning("pcre_exec() failed with error code %d", match_result);
      break;
    }
  }

  return mapping;
}

bool
UrlMappingPathContainer::processPathRegexMapping(UrlMappingRegexMatcher *reg_map)
{
  url_mapping *mapping;
  const char *path;
  char from_path[1024];
  const char *to_path;
  int from_path_len;
  int to_path_len;

  mapping = reg_map->getMapping();
  path = mapping->fromURL.path_get(&from_path_len);
  if (from_path_len >= (int)sizeof(from_path)) {
    Warning("path is too long, exceeds %d", (int)sizeof(from_path));
    return false;
  }

  memcpy(from_path, path, from_path_len);
  *(from_path + from_path_len) = '\0';
  to_path = mapping->toUrl.path_get(&to_path_len);
  return reg_map->init(from_path, to_path, to_path_len);
}

