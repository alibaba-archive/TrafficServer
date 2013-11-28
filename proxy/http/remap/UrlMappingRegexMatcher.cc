#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "libts.h"
#include "UrlMappingRegexMatcher.h"

UrlMappingRegexMatcher::UrlMappingRegexMatcher(url_mapping *mapping) :
  re(NULL), re_extra(NULL), to_template(NULL), to_template_len(0),
  n_substitutions(0), url_map(mapping)
{
}

UrlMappingRegexMatcher::~UrlMappingRegexMatcher()
{
  if (this->re != NULL) {
    pcre_free(this->re);
    this->re = NULL;
  }
  if (this->re_extra != NULL) {
    pcre_free(this->re_extra);
    this->re_extra = NULL;
  }
  if (this->to_template != NULL) {
    ats_free(this->to_template);
    this->to_template = NULL;
    this->to_template_len = 0;
  }
  if (this->url_map != NULL) {
    delete this->url_map;
    this->url_map = NULL;
  }
}

int
UrlMappingRegexMatcher::match(const char *input, const int input_len,
    char *output, const int out_size, int *out_len)
{
  int matches[MAX_REGEX_SUBS * 3];
  int match_result;

  match_result = pcre_exec(this->re, this->re_extra, input, input_len,
      0, 0, matches, (sizeof(matches) / sizeof(int)));
  if (match_result > 0) {
    *out_len = expandSubstitutions(matches, input, output, out_size);
    if (*out_len < 0) {
      return PCRE_ERROR_NOMATCH;
    }
  }
  return match_result;
}

bool
UrlMappingRegexMatcher::init(const char *pattern, const char *to_str, const int to_len)
{
  bool result;
  const char *str;
  int str_index;
  int substitution_id;

  // using pattern (and not mapping->fromURL.path_get())
  // as this one will be NULL-terminated (required by pcre_compile)
  this->re = pcre_compile(pattern, 0, &str, &str_index, NULL);
  if (this->re == NULL) {
    Warning("pcre_compile failed! Regex has error starting at %s", pattern + str_index);
    return false;
  }

  this->re_extra = pcre_study(this->re, 0, &str);
  if ((this->re_extra == NULL) && (str != NULL)) {
    Warning("pcre_study failed with message [%s]", str);
    return false;
  }

  int n_captures;
  if (pcre_fullinfo(this->re, this->re_extra, PCRE_INFO_CAPTURECOUNT, &n_captures) != 0) {
    Warning("pcre_fullinfo failed!");
    return false;
  }
  if (n_captures >= MAX_REGEX_SUBS) { // off by one for $0 (implicit capture)
    Warning("Regex has %d capturing subpatterns (including entire regex); Max allowed: %d",
            n_captures + 1, MAX_REGEX_SUBS);
    return false;
  }

  result = true;
  for (int i = 0; i < (to_len - 1); ++i) {
    if (to_str[i] == '$') {
      if (this->n_substitutions >= MAX_REGEX_SUBS) {
        Warning("Can not have more than %d substitutions in [%.*s]",
                MAX_REGEX_SUBS, to_len, to_str);
        result = false;
        break;
      }
      substitution_id = to_str[i + 1] - '0';
      if ((substitution_id < 0) || (substitution_id > n_captures)) {
        Warning("Substitution id [%c] has no corresponding capture pattern in regex [%s]",
              to_str[i + 1], pattern);
        result = false;
        break;
      }
      this->substitution_markers[this->n_substitutions] = i;
      this->substitution_ids[this->n_substitutions] = substitution_id;
      ++this->n_substitutions;
    }
  }

  if (result) {
    this->to_template_len = to_len;
    this->to_template = static_cast<char *>(ats_malloc(this->to_template_len));
    memcpy(this->to_template, to_str, this->to_template_len);
  }

  return result;
}

#define CHECK_BUFFER_SIZE(bytes) \
  do { \
    if ((cur_buf_size + bytes) > out_size) { \
      Warning("Overflow while expanding substitutions"); \
      return -1; \
    } \
  } while (0)

// does not null terminate return string
int
UrlMappingRegexMatcher::expandSubstitutions(int *matches,
    const char *input, char *output, int out_size)
{
  int cur_buf_size = 0;
  int token_start = 0;
  int n_bytes_needed;
  int match_index;

  for (int i = 0; i < this->n_substitutions; ++i) {
    // first copy preceding bytes
    n_bytes_needed = this->substitution_markers[i] - token_start;
    CHECK_BUFFER_SIZE(n_bytes_needed);
    memcpy(output + cur_buf_size, this->to_template + token_start, n_bytes_needed);
    cur_buf_size += n_bytes_needed;

    // then copy the sub pattern match
    match_index = this->substitution_ids[i] * 2;
    n_bytes_needed = matches[match_index + 1] - matches[match_index];
    CHECK_BUFFER_SIZE(n_bytes_needed);
    memcpy(output + cur_buf_size, input + matches[match_index], n_bytes_needed);
    cur_buf_size += n_bytes_needed;

    token_start = this->substitution_markers[i] + 2; // skip the place holder as $#
  }

  // copy last few bytes (if any)
  if (token_start < this->to_template_len) {
    n_bytes_needed = this->to_template_len - token_start;
    CHECK_BUFFER_SIZE(n_bytes_needed);
    memcpy(output + cur_buf_size, this->to_template + token_start, n_bytes_needed);
    cur_buf_size += n_bytes_needed;
  }
  return cur_buf_size;
}

