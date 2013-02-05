#ifndef _ACL_DIRECTIVE_H
#define _ACL_DIRECTIVE_H

#include <string.h>
#include "RemapDirective.h"

class ACLRefererDirective : public RemapDirective {
  public:
    ACLRefererDirective();
    ~ACLRefererDirective() {}

    DirectiveParams *newDirectiveParams(const int lineNo, 
        const char *lineStr, const int lineLen, DirectiveParams *parent, 
        const char *paramStr, const int paramLen, const bool bBlock);
};

class ACLSrcIpDirective : public RemapDirective {
  public:
    ACLSrcIpDirective();
    ~ACLSrcIpDirective() {}

    DirectiveParams *newDirectiveParams(const int lineNo, 
        const char *lineStr, const int lineLen, DirectiveParams *parent, 
        const char *paramStr, const int paramLen, const bool bBlock);
};


class ACLMethodDirective : public RemapDirective {
  public:
    ACLMethodDirective();
    ~ACLMethodDirective() {}

    DirectiveParams *newDirectiveParams(const int lineNo, 
        const char *lineStr, const int lineLen, DirectiveParams *parent, 
        const char *paramStr, const int paramLen, const bool bBlock);
};


class ACLRedirectUrlDirective : public RemapDirective {
  public:
    ACLRedirectUrlDirective();
    ~ACLRedirectUrlDirective() {}

    DirectiveParams *newDirectiveParams(const int lineNo, 
        const char *lineStr, const int lineLen, DirectiveParams *parent, 
        const char *paramStr, const int paramLen, const bool bBlock);
};

class ACLDirective : public RemapDirective {
  public:
    ACLDirective();
    ~ACLDirective() {}

    DirectiveParams *newDirectiveParams(const int lineNo, 
        const char *lineStr, const int lineLen, DirectiveParams *parent, 
        const char *paramStr, const int paramLen, const bool bBlock);

    inline ACLMethodDirective *getMethodDirective() {
      return _methodDirective;
    }

    inline ACLRefererDirective *getRefererDirective() {
      return _refererDirective;
    }

    inline ACLSrcIpDirective *getSrcIpDirective() {
      return _srcIpDirective;
    }

    inline ACLRedirectUrlDirective *getRedirectUrlDirective() {
      return _redirectUrlDirective;
    }

  protected:
    ACLMethodDirective *_methodDirective;
    ACLRefererDirective *_refererDirective;
    ACLSrcIpDirective *_srcIpDirective;
    ACLRedirectUrlDirective *_redirectUrlDirective;
};


#endif

