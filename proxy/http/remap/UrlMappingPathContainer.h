
#ifndef _URL_MAPPING_PATH_CONTAINER_
#define _URL_MAPPING_PATH_CONTAINER_

#include "UrlMapping.h"
#include "UrlMappingRegexMatcher.h"
#include "UrlMappingPathIndex.h"

class UrlMappingPathContainer
{
  public:
    UrlMappingPathContainer();

    virtual ~UrlMappingPathContainer();

    bool Insert(url_mapping *mapping);

    url_mapping *Search(URL *request_url, UrlMappingContainer &mapping_container);

    void Print();
  private:
    bool processPathRegexMapping(UrlMappingRegexMatcher *reg_map);

    UrlMappingPathIndex _path_tries;
    UrlMappingRegexList _path_regex_list;
    int _path_trie_min_rank;
    int _regex_min_rank;
};

#endif

