#ifndef _ACL_CHECKER_H
#define _ACL_CHECKER_H

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <arpa/inet.h>
#include "MappingTypes.h"
#include "RemapDirective.h"
#include "DirectiveParams.h"
#include "ACLParams.h"
#include "HostnameTrie.h"

class ACLCheckList;

class ACLChecker {
  friend class ACLCheckList;
  friend class ACLMethodIpCheckList;
  friend class ACLRefererCheckList;

  public:
    virtual ~ACLChecker() {}

    inline int getAction() const {
      return _action;
    }

    inline const char *getActionCaption() const {
      if (_action == ACL_ACTION_ALLOW_INT) {
        return ACL_ACTION_ALLOW_STR;
      }
      else if(_action == ACL_ACTION_DENY_INT) {
        return ACL_ACTION_DENY_STR;
      }
      else {
        return ACL_STR_UNKOWN;
      }
    }

    virtual bool match(const ACLContext & context) = 0;

    inline ACLChecker * next() const {
      return _next;
    }

    virtual void print(const char *prefix) = 0;

    void print() {
      char prefix[64];
      sprintf(prefix, "%s %s ", DIRECTVIE_NAME_ACL,
          this->getActionCaption());
      this->print(prefix);
    }

  protected:
    ACLChecker(const int action) : _action(action), _next(NULL) {}

    int _action;  //allow or deny
    ACLChecker *_next; //for ACLCheckList
};

//all are matched
class ACLAllChecker : public ACLChecker {
  public:
    ACLAllChecker(const int action) : ACLChecker(action) {}

    bool match(const ACLContext & context) {
      return true;
    }

    void print(const char *prefix) {
      printf("\t%s %s %s\n", DIRECTVIE_NAME_ACL,
          this->getActionCaption(), ACL_STR_ALL);
    }
};

class ACLMethodChecker : public ACLChecker {
  public:
    ACLMethodChecker(const int action) : ACLChecker(action), _methodFlags(0) {}

    bool match(const ACLContext & context) {
      return (_methodFlags & ACLMethodParams::getMethodFlag(&context.method)) != 0;
    }

    void add(const int methodFlags) {
      _methodFlags |= methodFlags;
    }

    inline int getMethodFlags() const {
      return _methodFlags;
    }

    void print(const char *prefix) {
        char buff[128];
        int len;
        printf("\t%s%s %s\n", prefix, DIRECTVIE_NAME_ACL_METHOD,
            ACLMethodParams::getMethodString(
              _methodFlags, buff, &len));
    }

  protected:
    int _methodFlags;
};

struct SrcIpNode {
  unsigned long start;    // IPv4 address value stores start of a range (host byte order)
  unsigned long end;      // IPv4 address value stores end of a range (host byte order)
  SrcIpNode *next;
};

class ACLSrcIpChecker: public ACLChecker {
  public:
    ACLSrcIpChecker(const int action) : ACLChecker(action),
    _srcIpsHead(NULL), _srcIpsTail(NULL) {}

    ~ACLSrcIpChecker();

    bool match(const ACLContext & context);
    bool add(const StringValue & srcIp);

    void print(const char *prefix) {
      int count;
      char buff1[64];
      char buff2[64];
      count = 0;
      SrcIpNode *node = _srcIpsHead;

      printf("\t%s%s ", prefix, DIRECTVIE_NAME_ACL_SRC_IP);
      while (node != NULL) {
        if (count++ > 0) {
          printf(" | ");
        }

        if (node->start == node->end) {
          printf("%s", this->inet_ntop(ntohl(node->start),
                buff1, sizeof(buff1)));
        }
        else if (node->start == 0 && node->end == ULONG_MAX) {
          printf("%s", ACL_STR_ALL);
        }
        else {
          printf("%s-%s", this->inet_ntop(ntohl(node->start),
                buff1, sizeof(buff1)), this->inet_ntop(ntohl(node->end),
                buff2, sizeof(buff2)));
        }
        node = node->next;
      }
      printf("\n");
    }

  protected:
    const char *inet_ntop(const unsigned long addr,
        char *buff, const int buffSize)
    {
      struct in_addr ip_addr;
      ip_addr.s_addr = addr;
      if (::inet_ntop(AF_INET, &ip_addr, buff, buffSize) == NULL) {
        *buff = '\0';
      }
      return buff;
    }

    SrcIpNode *_srcIpsHead;
    SrcIpNode *_srcIpsTail;
};

class ACLRefererEmptyChecker : public ACLChecker {
  public:
    ACLRefererEmptyChecker(const int action) : ACLChecker(action) {}

    bool match(const ACLContext & context) {
      return (context.refererUrl.length == 0);
    }

    void print(const char *prefix) {
        printf("\t%s%s %s\n", prefix,
            DIRECTVIE_NAME_ACL_REFERER, ACL_STR_EMPTY);
    }
};

class ACLRefererHostChecker: public ACLChecker {
  public:
    ACLRefererHostChecker(const int action) : ACLChecker(action) {}
    ~ACLRefererHostChecker() {}

    bool match(const ACLContext & context) {
      return _hostTrie.contains(context.refererHostname.str,
          context.refererHostname.length);
    }

    inline bool addHostname(const char *hostname, const int length)
    {
      return _hostTrie.insert(hostname, length);
    }

    bool addDomain(const char *domain, const int length)
    {
      char hostname[length + 2];

      *hostname = '.';
      memcpy(hostname + 1, domain, length);
      return _hostTrie.insert(hostname, length + 1);
    }

    void print(const char *prefix) {
      int count;
      char **hostnames = _hostTrie.getHostnames(&count);
      if (hostnames == NULL) {
        return;
      }

      int hostnameCount = 0;
      int domainCount = 0;
      int i;
      for (i=0; i<count; i++) {
        if (*hostnames[i] == '.') {
          if (domainCount++ == 0) {
            printf("\t%s%s %s ", prefix, DIRECTVIE_NAME_ACL_REFERER,
                ACL_REFERER_TYPE_DOMAIN_STR);
          }
          else {
            printf(" | ");
          }

          printf("%s", hostnames[i] + 1);
        }
      }
      if (domainCount > 0) {
        printf("\n");
      }

      for (i=0; i<count; i++) {
        if (*hostnames[i] != '.') {
          if (hostnameCount++ == 0) {
            printf("\t%s%s %s ", prefix, DIRECTVIE_NAME_ACL_REFERER,
                ACL_REFERER_TYPE_HOST_STR);
          }
          else {
            printf(" | ");
          }

          printf("%s", hostnames[i]);
        }
      }
      if (hostnameCount > 0) {
        printf("\n");
      }
    }

  protected:
    HostnameTrieSet _hostTrie;
};

struct RegexNode {
  char *pattern;
  pcre *re;
  RegexNode *next;
};

class ACLRefererRegexChecker: public ACLChecker {
  public:
    ACLRefererRegexChecker(const int action) : ACLChecker(action),
    _regexsHead(NULL), _regexsTail(NULL) {}

    ~ACLRefererRegexChecker();

    bool match(const ACLContext & context);
    bool add(const StringValue & regex);
    bool add(const char *regex);

    void print(const char *prefix) {
      RegexNode * node = _regexsHead;
      while (node != NULL) {
        printf("\t%s%s %s %s\n", prefix, DIRECTVIE_NAME_ACL_REFERER,
            ACL_REFERER_TYPE_REGEX_STR, node->pattern);
        node = node->next;
      }
    }

  protected:
    RegexNode *_regexsHead;
    RegexNode *_regexsTail;
};

class ACLRefererChecker: public ACLChecker {
  public:
    ACLRefererChecker(const int action);
    ~ACLRefererChecker();

    bool match(const ACLContext & context);
    bool add(const ACLRefererParams *refererParams);

    inline bool empty() {
      return (!_allMatched && _emptyChecker == NULL &&
          _hostChecker == NULL && _regexChecker == NULL);
    }

    inline bool needCheckRefererHost() const {
      return _hostChecker != NULL;
    }

    void print(const char *prefix) {
      if (_allMatched) {
        printf("\t%s%s %s\n", prefix,
            DIRECTVIE_NAME_ACL_REFERER, ACL_STR_ALL);
      }

      if (_emptyChecker != NULL) {
        _emptyChecker->print(prefix);
      }

      if (_hostChecker != NULL) {
        _hostChecker->print(prefix);
      }

      if (_regexChecker != NULL) {
        _regexChecker->print(prefix);
      }
    }

  protected:
    bool _allMatched;  //such as: acl allow referer all
    ACLRefererEmptyChecker *_emptyChecker;
    ACLRefererHostChecker *_hostChecker;
    ACLRefererRegexChecker *_regexChecker;
};

#endif

