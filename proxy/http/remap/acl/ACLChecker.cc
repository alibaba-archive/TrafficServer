#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "ACLChecker.h"

ACLSrcIpChecker::~ACLSrcIpChecker()
{
  SrcIpNode *node;
  while (_srcIpsHead != NULL) {
    node = _srcIpsHead;
    _srcIpsHead = _srcIpsHead->next;

    free(node);
  }

  _srcIpsTail = NULL;
}

//TODo: remove this function!!!
static const char *ExtractIpRange(char *match_str, unsigned long *addr1, unsigned long *addr2)
{
#define IP_DELIM  "-/"

  bool mask = false;
  int mask_bits;
  int mask_val;
  int numToks;
  unsigned long addr1_local;
  unsigned long addr2_local;
  char *rangeTok[2];
  char *saveptr;

  if (strchr(match_str, '/') != NULL) {
    mask = true;
  }

  saveptr = NULL;
  rangeTok[0] = strtok_r(match_str, IP_DELIM, &saveptr);
  if (rangeTok[0] == NULL) {
    return "no IP address given";
  }

  rangeTok[1] = strtok_r(NULL, IP_DELIM, &saveptr);
  if (rangeTok[1] == NULL) {
    numToks = 1;
  }
  else {
    if (strtok_r(NULL, IP_DELIM, &saveptr) != NULL) {
      return "malformed IP range";
    }
    numToks = 2;
  }

  addr1_local = htonl(inet_addr(rangeTok[0]));

  if (addr1_local == (unsigned long) - 1 && strcmp(rangeTok[0], "255.255.255.255") != 0) {
    return "malformed ip address";
  }
  /* Handle a IP range */
  if (numToks == 2) {

    if (mask == true) {
      /* coverity[secure_coding] */
      if (sscanf(rangeTok[1], "%d", &mask_bits) != 1) {
        return "bad mask specification";
      }

      if (!(mask_bits >= 0 && mask_bits <= 32)) {
        return "invalid mask specification";
      }

      if (mask_bits == 32) {
        mask_val = 0;
      } else {
        mask_val = 0xffffffff >> mask_bits;
      }

      addr2_local = addr1_local | mask_val;
      addr1_local = addr1_local & (mask_val ^ 0xffffffff);

    } else {
      addr2_local = htonl(inet_addr(rangeTok[1]));
      if (addr2_local == (unsigned long) - 1 && strcmp(rangeTok[1], "255.255.255.255") != 0) {
        return "malformed ip address at range end";
      }
    }

    if (addr1_local > addr2_local) {
      return "range start greater than range end";
    }
  } else {
    addr2_local = addr1_local;
  }

  *addr1 = addr1_local;
  *addr2 = addr2_local;
  return NULL;
}

bool ACLSrcIpChecker::add(const StringValue & srcIp)
{
  SrcIpNode *node  = (SrcIpNode *)malloc(sizeof(SrcIpNode));
  if (node == NULL) {
    fprintf(stderr, "malloc %d bytes fail, error info: %s", 
        (int)sizeof(SrcIpNode), strerror(errno));
    return false;
  }

  if (srcIp.equals(ACL_STR_ALL, sizeof(ACL_STR_ALL) - 1)) {
    node->start = 0;
    node->end = ULONG_MAX;
  }
  else {
    char buff[srcIp.length + 1];
    const char *error;
    memcpy(buff, srcIp.str, srcIp.length);
    *(buff + srcIp.length) = '\0';

    if ((error=ExtractIpRange(buff, &node->start, &node->end)) != NULL) {
      fprintf(stderr, "parse IP value %s fail, error info: %s\n", buff, error);
      free(node);
      return false;
    }
  }

  node->next = NULL;
  if (_srcIpsHead == NULL) {
    _srcIpsHead = node;
  }
  else {
    _srcIpsTail->next = node;
  }
  _srcIpsTail = node;

  return true;
}

bool ACLSrcIpChecker::match(const ACLContext & context)
{
  SrcIpNode *node;
  node = _srcIpsHead;
  while (node != NULL) {
    if (context.clientIp >= node->start && context.clientIp <= node->end) {
      return true;
    }

    node = node->next;
  }

  return false;
}

ACLRefererRegexChecker::~ACLRefererRegexChecker()
{
  RegexNode *node;
  while (_regexsHead != NULL) {
    node = _regexsHead;
    _regexsHead = _regexsHead->next;

    pcre_free(node->re);
    free(node);
  }

  _regexsTail = NULL;
}

bool ACLRefererRegexChecker::add(const char *regex)
{
  pcre *re;
  const char *error;
  int strIndex;

  re = pcre_compile(regex, 0, &error, &strIndex, NULL);
  if (re == NULL) {
    fprintf(stderr, "pcre_compile failed! Regex has error starting at %s", 
        regex + strIndex);
    return false;
  }

  int bytes;
  int len = strlen(regex);
  bytes = sizeof(RegexNode) + (len + 1);
  RegexNode *node  = (RegexNode *)malloc(bytes);
  if (node == NULL) {
    fprintf(stderr, "malloc %d bytes fail, error info: %s", 
        bytes, strerror(errno));
    pcre_free(re);
    return false;
  }

  node->pattern = ((char *)(node)) + sizeof(RegexNode);
  memcpy(node->pattern, regex, len);
  *(node->pattern + len) = '\0';

  node->re = re;
  node->next = NULL;
  if (_regexsHead == NULL) {
    _regexsHead = node;
  }
  else {
    _regexsTail->next = node;
  }
  _regexsTail = node;

  return true;
}

bool ACLRefererRegexChecker::add(const StringValue & regex)
{
  char buff[regex.length + 1];
  memcpy(buff, regex.str, regex.length);
  *(buff + regex.length) = '\0';

  return this->add(buff);
}

bool ACLRefererRegexChecker::match(const ACLContext & context)
{
  int matches[30];
  RegexNode *node = _regexsHead;
  while (node != NULL) {
    if (pcre_exec(node->re, NULL, context.refererUrl.str, 
          context.refererUrl.length, 0, 0, matches, 
          (sizeof(matches) / sizeof(int))) > 0)
    {
      return true;
    }

    node = node->next;
  }

  return false;
}

ACLRefererChecker::ACLRefererChecker(const int action) : ACLChecker(action),
    _allMatched(false), _emptyChecker(NULL), _hostChecker(NULL),
    _regexChecker(NULL)
{
}

ACLRefererChecker::~ACLRefererChecker()
{
  if (_emptyChecker != NULL) {
    delete _emptyChecker;
    _emptyChecker = NULL;
  }

  if (_hostChecker != NULL) {
    delete _hostChecker;
    _hostChecker = NULL;
  }

  if (_regexChecker != NULL) {
    delete _regexChecker;
    _regexChecker = NULL;
  }
}

bool ACLRefererChecker::match(const ACLContext & context)
{
  if (_allMatched) {
    return true;
  }

  if (_emptyChecker != NULL) {
    if (_emptyChecker->match(context)) {
      return true;
    }
  }

  if (context.refererUrl.length == 0) {
    return false;
  }

  if (_hostChecker != NULL) {
    if (_hostChecker->match(context)) {
      return true;
    }
  }

  if (_regexChecker != NULL) {
    if (_regexChecker->match(context)) {
      return true;
    }
  }

  return false;
}

bool ACLRefererChecker::add(const ACLRefererParams *refererParams)
{
  int count;
  const StringValue *parts; 

  switch (refererParams->getRefererType()) {
    case ACL_REFERER_TYPE_EMPTY_INT:
      if (_emptyChecker != NULL) {
        return true;
      }

      _emptyChecker = new ACLRefererEmptyChecker(_action);
      if (_emptyChecker == NULL) {
        return false;
      }
      break;
    case ACL_REFERER_TYPE_ALL_INT:
      _allMatched = true;
      break;
    case ACL_REFERER_TYPE_HOST_INT:
    case ACL_REFERER_TYPE_DOMAIN_INT:
      if (_hostChecker == NULL) {
        _hostChecker = new ACLRefererHostChecker(_action);
        if (_hostChecker == NULL) {
          return false;
        }
      }
     
      count = refererParams->getPartCount();
      parts = refererParams->getParts();
      if (refererParams->getRefererType() == ACL_REFERER_TYPE_HOST_INT) {
        for (int i=0; i<count; i++) {
          if (!_hostChecker->addHostname(parts[i].str, parts[i].length)) {
            return false;
          }
        }
      }
      else {
        for (int i=0; i<count; i++) {
          if (!_hostChecker->addDomain(parts[i].str, parts[i].length)) {
            return false;
          }
        }
      }

      break;
    case ACL_REFERER_TYPE_REGEX_INT:
      if (_regexChecker == NULL) {
        _regexChecker = new ACLRefererRegexChecker(_action);
        if (_regexChecker == NULL) {
          return false;
        }
      }

      return _regexChecker->add(refererParams->getParts()[0]);
    default:
      return false;
  }

  return true;
}

