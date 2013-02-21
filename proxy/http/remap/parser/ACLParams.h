#ifndef _ACL_PARAMS_H
#define _ACL_PARAMS_H

#include <string.h>
#include "DirectiveParams.h"

#define ACL_SECOND_DIRECTIVE_UNKOWN_STR       "UNKOWN"
#define ACL_SECOND_DIRECTIVE_DEFINE_STR       "define"
#define ACL_SECOND_DIRECTIVE_CHECK_STR        "check"
#define ACL_SECOND_DIRECTIVE_ALLOW_STR        "allow"
#define ACL_SECOND_DIRECTIVE_DENY_STR         "deny"
#define ACL_SECOND_DIRECTIVE_REDIRECT_URL_STR "redirect_url"

#define ACL_SECOND_DIRECTIVE_NONE_INT         0
#define ACL_SECOND_DIRECTIVE_DEFINE_INT       1
#define ACL_SECOND_DIRECTIVE_CHECK_INT        2
#define ACL_SECOND_DIRECTIVE_ALLOW_INT        3
#define ACL_SECOND_DIRECTIVE_DENY_INT         4
#define ACL_SECOND_DIRECTIVE_REDIRECT_URL_INT 5

#define ACL_ACTION_ALLOW_STR   "allow"
#define ACL_ACTION_DENY_STR    "deny"

#define ACL_STR_ALL    "all"
#define ACL_STR_EMPTY  "empty"
#define ACL_STR_UNKOWN "UNKOWN"

#define ACL_SEPERATOR_CHAR '|'

#define ACL_METHOD_FLAG_NONE      0
#define ACL_METHOD_FLAG_CONNECT   (1 << 0)
#define ACL_METHOD_FLAG_DELETE    (1 << 1)
#define ACL_METHOD_FLAG_GET       (1 << 2)
#define ACL_METHOD_FLAG_HEAD      (1 << 3)
#define ACL_METHOD_FLAG_ICP_QUERY (1 << 4)
#define ACL_METHOD_FLAG_OPTIONS   (1 << 5)
#define ACL_METHOD_FLAG_POST      (1 << 6)
#define ACL_METHOD_FLAG_PURGE     (1 << 7)
#define ACL_METHOD_FLAG_PUT       (1 << 8)
#define ACL_METHOD_FLAG_TRACE     (1 << 9)
#define ACL_METHOD_FLAG_PUSH      (1 << 10)
#define ACL_METHOD_FLAG_ALL       (ACL_METHOD_FLAG_CONNECT | \
  ACL_METHOD_FLAG_DELETE | ACL_METHOD_FLAG_GET | ACL_METHOD_FLAG_HEAD | \
  ACL_METHOD_FLAG_ICP_QUERY | ACL_METHOD_FLAG_OPTIONS | \
  ACL_METHOD_FLAG_POST | ACL_METHOD_FLAG_PURGE | ACL_METHOD_FLAG_PUT | \
  ACL_METHOD_FLAG_TRACE | ACL_METHOD_FLAG_PUSH)

#define ACL_METHOD_MAX_NUM  12

#define ACL_REFERER_TYPE_UNKOWN_STR  "UNKOWN"
#define ACL_REFERER_TYPE_EMPTY_STR   "empty"
#define ACL_REFERER_TYPE_REGEX_STR   "regex"
#define ACL_REFERER_TYPE_HOST_STR    "host"
#define ACL_REFERER_TYPE_DOMAIN_STR  "domain"

#define ACL_REFERER_TYPE_NONE_INT    0
#define ACL_REFERER_TYPE_EMPTY_INT   1
#define ACL_REFERER_TYPE_ALL_INT     2
#define ACL_REFERER_TYPE_REGEX_INT   3
#define ACL_REFERER_TYPE_HOST_INT    4
#define ACL_REFERER_TYPE_DOMAIN_INT  5

#define ACL_REFERER_MAX_SPLIT_PARTS  128

/*
class ACLSubKeyParams : public DirectiveParams {
    ACLSubKeyParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);
  public:
     virtual ~ACLSubKeyParams() {}
};
*/

class ACLRedirectUrlParams : public DirectiveParams {
  public:
    ACLRedirectUrlParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock, const bool primaryDirective);
     ~ACLRedirectUrlParams() {}
     int parse(const char *blockStat, const char *blockEnd);

     const StringValue * getUrl() const {
       return &_url;
     }

  protected:
     const char *toString(char *buff, int *len);

     StringValue _url;
     bool _primaryDirective;  //if primary directive in acl define block
};


class ACLMethodParams : public DirectiveParams {
  public:
    ACLMethodParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);
     ~ACLMethodParams() {}

     int parse(const char *blockStat, const char *blockEnd);

     static int getMethodFlag(const StringValue *sv);

     inline int getMethodFlags() const {
       return _methodFlags;
     }

     static const char *getMethodString(const int methodFlags,
         char *buff, int *len);

  protected:
     const char *toString(char *buff, int *len);

     int _methodFlags;
};

class ACLSrcIpParams : public DirectiveParams {
  public:
    ACLSrcIpParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);

     ~ACLSrcIpParams() {}

     int parse(const char *blockStat, const char *blockEnd);

     inline const StringValue * getIp() const {
       return &_ip;
     }

  protected:
     const char *toString(char *buff, int *len);
     StringValue _ip;
};

class ACLRefererParams : public DirectiveParams {
  public:
    ACLRefererParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);
     ~ACLRefererParams() {}
     int parse(const char *blockStat, const char *blockEnd);

     inline int getRefererType() const {
       return _refererType;
     }

     inline int getPartCount() const {
       return _partCount;
     }

     inline const StringValue *getParts() const {
       return _parts;
     }

  protected:
     const char *toString(char *buff, int *len);

     inline const char *getRefererTypeString() {
       switch (_refererType) {
         case ACL_REFERER_TYPE_EMPTY_INT:
           return ACL_REFERER_TYPE_EMPTY_STR;
         case ACL_REFERER_TYPE_ALL_INT:
           return ACL_STR_ALL;
         case ACL_REFERER_TYPE_REGEX_INT:
           return ACL_REFERER_TYPE_REGEX_STR;
         case ACL_REFERER_TYPE_HOST_INT:
           return ACL_REFERER_TYPE_HOST_STR;
         case ACL_REFERER_TYPE_DOMAIN_INT:
           return ACL_REFERER_TYPE_DOMAIN_STR;
         default:
           return ACL_REFERER_TYPE_UNKOWN_STR;
       }
     }

     int _refererType;
     int _partCount;
     StringValue _parts[ACL_REFERER_MAX_SPLIT_PARTS];
};

/*
class ACLParams : public DirectiveParams {
  protected:
    ACLParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);

  public:
     virtual ~ACLParams() {}
  protected:
     inline int getACLAction(StringValue *sv) {
       if (sv->equals(ACL_SECOND_DIRECTIVE_DEFINE_STR,
             sizeof(ACL_SECOND_DIRECTIVE_DEFINE_STR) - 1))
       {
         return ACL_SECOND_DIRECTIVE_DEFINE_INT;
       }
       if (sv->equals(ACL_SECOND_DIRECTIVE_CHECK_STR,
             sizeof(ACL_SECOND_DIRECTIVE_CHECK_STR) - 1))
       {
         return ACL_SECOND_DIRECTIVE_CHECK_INT;
       }
       if (sv->equals(ACL_SECOND_DIRECTIVE_ALLOW_STR,
             sizeof(ACL_SECOND_DIRECTIVE_ALLOW_STR) - 1))
       {
         return ACL_SECOND_DIRECTIVE_ALLOW_INT;
       }
       if (sv->equals(ACL_SECOND_DIRECTIVE_DENY_STR,
             sizeof(ACL_SECOND_DIRECTIVE_DENY_STR) - 1))
       {
         return ACL_SECOND_DIRECTIVE_DENY_INT;
       }
       if (sv->equals(ACL_SECOND_DIRECTIVE_REDIRECT_URL_STR,
             sizeof(ACL_SECOND_DIRECTIVE_REDIRECT_URL_STR) - 1))
       {
         return ACL_SECOND_DIRECTIVE_REDIRECT_URL_INT;
       }

       return ACL_SECOND_DIRECTIVE_NONE_INT;
     }

     inline const char *getActionCaption() {
       switch(_action) {
         case ACL_SECOND_DIRECTIVE_DEFINE_INT:
           return ACL_SECOND_DIRECTIVE_DEFINE_STR;
         case ACL_SECOND_DIRECTIVE_CHECK_INT:
           return ACL_SECOND_DIRECTIVE_CHECK_STR;
         case ACL_SECOND_DIRECTIVE_ALLOW_INT:
           return ACL_SECOND_DIRECTIVE_ALLOW_STR;
         case ACL_SECOND_DIRECTIVE_DENY_INT:
           return ACL_SECOND_DIRECTIVE_DENY_STR;
         case ACL_SECOND_DIRECTIVE_REDIRECT_URL_INT:
           return ACL_SECOND_DIRECTIVE_REDIRECT_URL_STR;
         default:
           return ACL_SECOND_DIRECTIVE_UNKOWN_STR;
       }
     }

};
*/

//acl define <acl-name> <allow | deny>
class ACLDefineParams : public DirectiveParams {
  public:
    ACLDefineParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);
    ~ACLDefineParams() {}
    int parse(const char *blockStat, const char *blockEnd);

    inline int getAction() const {
      return _action;
    }

    inline const StringValue *getAclName() const {
      return &_aclName;
    }

  protected:
    const char *toString(char *buff, int *len);

    inline const char *getActionString() {
      if (_action == ACL_ACTION_ALLOW_INT) {
        return ACL_ACTION_ALLOW_STR;
      }
      else if (_action == ACL_ACTION_DENY_INT) {
        return ACL_ACTION_DENY_STR;
      }
      else {
        return "";
      }
    }

    StringValue _aclName;
    int _action;  //allow or deny
};

//acl check xxx
class ACLCheckParams : public DirectiveParams {
  public:
    ACLCheckParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);
    ~ACLCheckParams() {}

    int parse(const char *blockStat, const char *blockEnd);

    const StringValue * getAclName() const {
      return &_aclName;
    }

  protected:
    const char *toString(char *buff, int *len);

    StringValue _aclName;
};

class ACLActionParams : public DirectiveParams {
  protected:
    ACLActionParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);

    virtual ~ACLActionParams() {
      if (_actionParams != NULL) {
        delete _actionParams;
        _actionParams = NULL;
      }
    }

    int parse(const char *blockStat, const char *blockEnd);

  public:
    inline int getAction() const {
      return _action;
    }

    inline const DirectiveParams *getActionParams() const {
      return _actionParams;
    }

  protected:
    const char *toString(char *buff, int *len);

    inline const char *getActionString() {
      if (_action == ACL_ACTION_ALLOW_INT) {
        return ACL_ACTION_ALLOW_STR;
      }
      else if (_action == ACL_ACTION_DENY_INT) {
        return ACL_ACTION_DENY_STR;
      }
      else {
        return "";
      }
    }

    int _action;  //allow or deny
    DirectiveParams *_actionParams;  //NULL for all
};

//acl allow xxx
class ACLAllowParams : public ACLActionParams {
  public:
    ACLAllowParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);
    ~ACLAllowParams() {}
};

//acl deny xxx
class ACLDenyParams : public ACLActionParams {
  public:
    ACLDenyParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);
    ~ACLDenyParams() {}
};

#endif

