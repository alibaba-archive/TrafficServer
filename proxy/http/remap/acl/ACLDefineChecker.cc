#include "ACLDefineChecker.h"
#include "RemapDirective.h"

ACLDefineChecker::ACLDefineChecker() : ACLChecker(0),
    _methodChecker(NULL), _srcIpChecker(NULL), _refererChecker(NULL),
    _defineNext(NULL)
{
    *_aclName = '\0';
    *_redirectUrl = '\0';
}

ACLDefineChecker::~ACLDefineChecker()
{
  if (_methodChecker != NULL) {
    delete _methodChecker;
    _methodChecker = NULL;
  }

  if (_srcIpChecker != NULL) {
    delete _srcIpChecker;
    _srcIpChecker = NULL;
  }

  if (_refererChecker != NULL) {
    delete _refererChecker;
    _refererChecker = NULL;
  }
}

void ACLDefineChecker::print(const char *prefix)
{
  printf("\tacl %s %s %s {\n", ACL_SECOND_DIRECTIVE_DEFINE_STR, _aclName,
     (_action == ACL_ACTION_ALLOW_INT ? ACL_ACTION_ALLOW_STR :
     ACL_ACTION_DENY_STR));
  if (*_redirectUrl != '\0') {
    printf("\t\t%s %s\n", ACL_SECOND_DIRECTIVE_REDIRECT_URL_STR,
        _redirectUrl);
  }

  if (_methodChecker != NULL) {
    _methodChecker->print("\t");
  }

  if (_srcIpChecker != NULL) {
    _srcIpChecker->print("\t");
  }

  if (_refererChecker != NULL) {
    _refererChecker->print("\t");
  }

  printf("\t}\n");
}

bool ACLDefineChecker::init(const ACLDefineParams *defineParams)
{
  _action = defineParams->getAction();
  const StringValue *aclName = defineParams->getAclName();
  snprintf(_aclName, sizeof(_aclName), "%.*s", aclName->length, aclName->str);

  const ACLRedirectUrlParams *redirectUrlParams;
  const ACLMethodParams *methodParams;
  const ACLSrcIpParams *srcIpParams;
  const ACLRefererParams *refererParams;
  const DirectiveParams *children = defineParams->getChildren();
  while (children != NULL) {
    if ((redirectUrlParams=dynamic_cast<const ACLRedirectUrlParams *>
          (children)) != NULL)
    {
      snprintf(_redirectUrl, sizeof(_redirectUrl), "%.*s",
          redirectUrlParams->getUrl()->length,
          redirectUrlParams->getUrl()->str);
    }
    else if ((methodParams=dynamic_cast<const ACLMethodParams *>
          (children)) != NULL)
    {
      if (_methodChecker == NULL) {
        _methodChecker = new ACLMethodChecker(_action);
        if (_methodChecker == NULL) {
          return false;
        }
      }

      _methodChecker->add(methodParams->getMethodFlags());
    }
    else if ((srcIpParams=dynamic_cast<const ACLSrcIpParams *>
          (children)) != NULL)
    {
      if (_srcIpChecker == NULL) {
        _srcIpChecker = new ACLSrcIpChecker(_action);
        if (_srcIpChecker == NULL) {
          return false;
        }
      }

      _srcIpChecker->add(*(srcIpParams->getIp()));
    }
    else if ((refererParams=dynamic_cast<const ACLRefererParams *>
          (children)) != NULL)
    {
      if (_refererChecker == NULL) {
        _refererChecker = new ACLRefererChecker(_action);
        if (_refererChecker == NULL) {
          return false;
        }
      }

      _refererChecker->add(refererParams);
    }

    children = children->next();
  }

  return true;
}

bool ACLDefineChecker::match(const ACLContext & context)
{
  if (_methodChecker != NULL) {
    if (!_methodChecker->match(context)) {
      return false;
    }
  }

  if (_srcIpChecker != NULL) {
    if (!_srcIpChecker->match(context)) {
      return false;
    }
  }

  if (_refererChecker != NULL) {
    return _refererChecker->match(context);
  }
  else {
    return false;
  }
}

