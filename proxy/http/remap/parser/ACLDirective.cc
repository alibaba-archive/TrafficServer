#include "ACLDirective.h"
#include "ACLParams.h"

ACLDirective::ACLDirective() : 
  RemapDirective(DIRECTVIE_NAME_ACL, DIRECTIVE_TYPE_BOTH, 2, 4)
{
  int index = 0;

  _methodDirective = new ACLMethodDirective();
  _refererDirective = new ACLRefererDirective();
  _srcIpDirective = new ACLSrcIpDirective();
  _redirectUrlDirective = new ACLRedirectUrlDirective();

  this->_children[index++] = _methodDirective;
  this->_children[index++] = _redirectUrlDirective;
  this->_children[index++] = _refererDirective;
  this->_children[index++] = _srcIpDirective;
  this->_childrenCount = index;
}

DirectiveParams *ACLDirective::newDirectiveParams(const int lineNo, 
    const char *lineStr, const int lineLen, DirectiveParams *parent, 
    const char *paramStr, const int paramLen, const bool bBlock)
{
  StringValue firstParam;
  if (DirectiveParams::getFirstParam(paramStr, paramLen, &firstParam)
      != 0)
  {
    return NULL;
  }

  if (firstParam.equals(ACL_SECOND_DIRECTIVE_DEFINE_STR, 
        sizeof(ACL_SECOND_DIRECTIVE_DEFINE_STR) - 1))
  {
    return new ACLDefineParams(lineNo, lineStr, lineLen, parent, this, 
              paramStr, paramLen, bBlock);
  }
  else if (firstParam.equals(ACL_SECOND_DIRECTIVE_CHECK_STR, 
        sizeof(ACL_SECOND_DIRECTIVE_CHECK_STR) - 1))
  {
    return new ACLCheckParams(lineNo, lineStr, lineLen, parent, this, 
              paramStr, paramLen, bBlock);
  }
  else if (firstParam.equals(ACL_SECOND_DIRECTIVE_ALLOW_STR, 
        sizeof(ACL_SECOND_DIRECTIVE_ALLOW_STR) - 1))
  {
    return new ACLAllowParams(lineNo, lineStr, lineLen, parent, this, 
              paramStr, paramLen, bBlock);
  }
  else if (firstParam.equals(ACL_SECOND_DIRECTIVE_DENY_STR, 
        sizeof(ACL_SECOND_DIRECTIVE_DENY_STR) - 1))
  {
    return new ACLDenyParams(lineNo, lineStr, lineLen, parent, this, 
              paramStr, paramLen, bBlock);
  }
  else if (firstParam.equals(ACL_SECOND_DIRECTIVE_REDIRECT_URL_STR, 
        sizeof(ACL_SECOND_DIRECTIVE_REDIRECT_URL_STR) - 1))
  {
    return new ACLRedirectUrlParams(lineNo, lineStr, lineLen, parent, this, 
              paramStr, paramLen, bBlock, false);
  }
  else {
    return NULL;
  }
}


ACLRefererDirective::ACLRefererDirective() : 
  RemapDirective(DIRECTVIE_NAME_ACL_REFERER, DIRECTIVE_TYPE_STATEMENT, 1, 2)
{
}

DirectiveParams *ACLRefererDirective::newDirectiveParams(const int lineNo, 
    const char *lineStr, const int lineLen, DirectiveParams *parent, 
    const char *paramStr, const int paramLen, const bool bBlock)
{
    return new ACLRefererParams(lineNo, lineStr, lineLen, parent, this, 
              paramStr, paramLen, bBlock);
}


ACLSrcIpDirective::ACLSrcIpDirective() : 
  RemapDirective(DIRECTVIE_NAME_ACL_SRC_IP, DIRECTIVE_TYPE_STATEMENT, 1, 1)
{
}

DirectiveParams *ACLSrcIpDirective::newDirectiveParams(const int lineNo, 
    const char *lineStr, const int lineLen, DirectiveParams *parent, 
    const char *paramStr, const int paramLen, const bool bBlock)
{
    return new ACLSrcIpParams(lineNo, lineStr, lineLen, parent, this, 
              paramStr, paramLen, bBlock);
}


ACLMethodDirective::ACLMethodDirective() : 
  RemapDirective(DIRECTVIE_NAME_ACL_METHOD, DIRECTIVE_TYPE_STATEMENT, 1, 1)
{
}

DirectiveParams *ACLMethodDirective::newDirectiveParams(const int lineNo, 
    const char *lineStr, const int lineLen, DirectiveParams *parent, 
    const char *paramStr, const int paramLen, const bool bBlock)
{
    return new ACLMethodParams(lineNo, lineStr, lineLen, parent, this, 
              paramStr, paramLen, bBlock);
}


ACLRedirectUrlDirective::ACLRedirectUrlDirective() : 
  RemapDirective(DIRECTVIE_NAME_ACL_REDIRECT_URL, DIRECTIVE_TYPE_STATEMENT, 1, 1)
{
}

DirectiveParams *ACLRedirectUrlDirective::newDirectiveParams(const int lineNo, 
    const char *lineStr, const int lineLen, DirectiveParams *parent, 
    const char *paramStr, const int paramLen, const bool bBlock)
{
    return new ACLRedirectUrlParams(lineNo, lineStr, lineLen, parent, this, 
              paramStr, paramLen, bBlock, true);
}

