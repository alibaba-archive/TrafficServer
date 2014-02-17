
#ifndef _URL_MAPPING_REGEX_MATCHER
#define _URL_MAPPING_REGEX_MATCHER

#include "UrlMapping.h"

class UrlMappingRegexMatcher
{
  public:
    static const int MAX_REGEX_SUBS = 10;

    UrlMappingRegexMatcher(url_mapping *mapping);
    ~UrlMappingRegexMatcher();

    inline url_mapping *getMapping() {
      return this->url_map;
    }

    bool init(const char *pattern, const char *to_str, const int to_len);

    int match(const char *input, const int input_len,
        char *output, const int out_size, int *out_len);

    LINK(UrlMappingRegexMatcher, link);

  private:
    int expandSubstitutions(int *matches, const char *input,
        char *output, const int out_size);

    pcre *re;
    pcre_extra *re_extra;

    // we store the host-string-to-substitute here; if a match is found,
    // the substitutions are made and the resulting url is stored
    // directly in toURL's host field
    char *to_template;
    int to_template_len;

    // stores the number of substitutions
    int n_substitutions;

    // these two together point to template string places where
    // substitutions need to be made and the matching substring
    // to use
    int substitution_markers[MAX_REGEX_SUBS];
    int substitution_ids[MAX_REGEX_SUBS];

    url_mapping *url_map;
};

typedef Queue<UrlMappingRegexMatcher> UrlMappingRegexList;

#endif

