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

DirectiveParams *ACLDirective::newDirectiveParams(const int rank, const char *filename,
    const int lineNo, const char *lineStr, const int lineLen,
    DirectiveParams *parent, const char *paramStr, const int paramLen,
    const bool bBlock)
{
  StringValue firstParam;
  if (DirectiveParams::getFirstParam(paramStr, paramLen, &firstParam)
      != 0)
  {
    fprintf(stderr, "config file: %s, " \
        "empty acl parameter! config line no: %d, line: %.*s\n",
        filename, lineNo, lineLen, lineStr);
    return NULL;
  }

  if (firstParam.equals(ACL_SECOND_DIRECTIVE_DEFINE_STR,
        sizeof(ACL_SECOND_DIRECTIVE_DEFINE_STR) - 1))
  {
    return new ACLDefineParams(rank, filename, lineNo, lineStr, lineLen,
        parent, this, paramStr, paramLen, bBlock);
  }
  else if (firstParam.equals(ACL_SECOND_DIRECTIVE_CHECK_STR,
        sizeof(ACL_SECOND_DIRECTIVE_CHECK_STR) - 1))
  {
    return new ACLCheckParams(rank, filename, lineNo, lineStr, lineLen,
        parent, this, paramStr, paramLen, bBlock);
  }
  else if (firstParam.equals(ACL_SECOND_DIRECTIVE_ALLOW_STR,
        sizeof(ACL_SECOND_DIRECTIVE_ALLOW_STR) - 1))
  {
    return new ACLAllowParams(rank, filename, lineNo, lineStr, lineLen,
        parent, this, paramStr, paramLen, bBlock);
  }
  else if (firstParam.equals(ACL_SECOND_DIRECTIVE_DENY_STR,
        sizeof(ACL_SECOND_DIRECTIVE_DENY_STR) - 1))
  {
    return new ACLDenyParams(rank, filename, lineNo, lineStr, lineLen,
        parent, this, paramStr, paramLen, bBlock);
  }
  else if (firstParam.equals(ACL_SECOND_DIRECTIVE_REDIRECT_URL_STR,
        sizeof(ACL_SECOND_DIRECTIVE_REDIRECT_URL_STR) - 1))
  {
    return new ACLRedirectUrlParams(rank, filename, lineNo, lineStr, lineLen,
        parent, this, paramStr, paramLen, bBlock, false);
  }
  else {
    fprintf(stderr, "config file: %s, " \
        "invalid acl parameter: %.*s! config line no: %d, line: %.*s\n",
        filename, firstParam.length, firstParam.str, lineNo, lineLen, lineStr);
    return NULL;
  }
}


ACLRefererDirective::ACLRefererDirective() :
  RemapDirective(DIRECTVIE_NAME_ACL_REFERER, DIRECTIVE_TYPE_STATEMENT, 1, 2)
{
}

DirectiveParams *ACLRefererDirective::newDirectiveParams(const int rank, const char *filename,
    const int lineNo, const char *lineStr, const int lineLen,
    DirectiveParams *parent, const char *paramStr, const int paramLen,
    const bool bBlock)
{
    return new ACLRefererParams(rank, filename, lineNo, lineStr, lineLen,
        parent, this, paramStr, paramLen, bBlock);
}


ACLSrcIpDirective::ACLSrcIpDirective() :
  RemapDirective(DIRECTVIE_NAME_ACL_SRC_IP, DIRECTIVE_TYPE_STATEMENT, 1, 1)
{
}

DirectiveParams *ACLSrcIpDirective::newDirectiveParams(const int rank, const char *filename,
    const int lineNo, const char *lineStr, const int lineLen,
    DirectiveParams *parent, const char *paramStr, const int paramLen,
    const bool bBlock)
{
    return new ACLSrcIpParams(rank, filename, lineNo, lineStr, lineLen,
        parent, this, paramStr, paramLen, bBlock);
}


ACLMethodDirective::ACLMethodDirective() :
  RemapDirective(DIRECTVIE_NAME_ACL_METHOD, DIRECTIVE_TYPE_STATEMENT, 1, 1)
{
}

DirectiveParams *ACLMethodDirective::newDirectiveParams(const int rank, const char *filename,
    const int lineNo, const char *lineStr, const int lineLen,
    DirectiveParams *parent, const char *paramStr, const int paramLen,
    const bool bBlock)
{
    return new ACLMethodParams(rank, filename, lineNo, lineStr, lineLen,
        parent, this, paramStr, paramLen, bBlock);
}


ACLRedirectUrlDirective::ACLRedirectUrlDirective() :
  RemapDirective(DIRECTVIE_NAME_ACL_REDIRECT_URL, DIRECTIVE_TYPE_STATEMENT, 1, 1)
{
}

DirectiveParams *ACLRedirectUrlDirective::newDirectiveParams(const int rank, const char *filename,
    const int lineNo, const char *lineStr, const int lineLen,
    DirectiveParams *parent, const char *paramStr, const int paramLen,
    const bool bBlock)
{
    return new ACLRedirectUrlParams(rank, filename, lineNo, lineStr, lineLen,
        parent, this, paramStr, paramLen, bBlock, true);
}

