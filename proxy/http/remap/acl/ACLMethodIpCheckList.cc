#include "ACLDefineManager.h"
#include "ACLDefineChecker.h"
#include "ACLMethodIpCheckList.h"

ACLMethodIpCheckList::ACLMethodIpCheckList() : ACLCheckList()
{
  _checkerCount = 3;
}

int ACLMethodIpCheckList::load(const DirectiveParams *parentParams)
{
  const ACLActionParams *actionParams;
  const ACLCheckParams *checkParams;

  const DirectiveParams *params;
  const ACLMethodParams *methodParams;
  const ACLSrcIpParams *srcIpParams;
  ACLChecker *lastChecker = NULL;
  int aclTotalDefineCount = 0;
  int aclMyDefineCount = 0;
  int aclAnyCount = 0;

  const DirectiveParams *children = parentParams->getChildren();
  while (children != NULL) {
    if ((checkParams=dynamic_cast<const ACLCheckParams *>
          (children)) != NULL)
    {
      lastChecker = ACLDefineManager::getInstance()->find(
          checkParams->getAclName());
      if (lastChecker == NULL) {
        fprintf(stderr, "Can't find acl name: %.*s!\n",
            checkParams->getAclName()->length,
            checkParams->getAclName()->str);
        return ENOENT;
      }

      aclTotalDefineCount++;
      if (!((ACLDefineChecker *)lastChecker)->isRefererChecker()) {
        this->addChecker(_checkers + ACL_FILED_INDEX_WHOLE_METHOD_IP, lastChecker);
        aclMyDefineCount++;
      }
    }
    else if ((actionParams=dynamic_cast<const ACLActionParams *>
          (children)) != NULL)
    {
      int action = actionParams->getAction();
      params = actionParams->getActionParams();
      if (params == NULL) {
        lastChecker = new ACLAllChecker(action);
        this->addChecker(_checkers + ACL_FILED_INDEX_WHOLE_METHOD_IP, lastChecker);
        aclAnyCount++;
      }
      else if ((methodParams=dynamic_cast<const ACLMethodParams *>
            (params)) != NULL)
      {
        ACLMethodChecker *methodChecker;
        if (lastChecker == NULL || !((methodChecker=dynamic_cast<
                ACLMethodChecker *>(lastChecker)) != NULL &&
              methodChecker->getAction() == action))
        {
          methodChecker = new ACLMethodChecker(action);
          if (methodChecker == NULL) {
            return ENOMEM;
          }

          this->addChecker(_checkers + ACL_FILED_INDEX_METHOD,
              methodChecker);
        }

        methodChecker->add(methodParams->getMethodFlags());
        lastChecker = methodChecker;
      }
      else if ((srcIpParams=dynamic_cast<const ACLSrcIpParams *>
            (params)) != NULL)
      {
        ACLSrcIpChecker *srcIpChecker;
        if (lastChecker == NULL || !((srcIpChecker=dynamic_cast<
                ACLSrcIpChecker *>(lastChecker)) != NULL &&
              srcIpChecker->getAction() == action))
        {
          srcIpChecker = new ACLSrcIpChecker(action);
          if (srcIpChecker == NULL) {
            return ENOMEM;
          }

          this->addChecker(_checkers + ACL_FILED_INDEX_SRC_IP,
              srcIpChecker);
        }

        srcIpChecker->add(*(srcIpParams->getIp()));
        lastChecker = srcIpChecker;
      }
    }

    children = children->next();
  }

  if (aclAnyCount > 0 && (aclTotalDefineCount > 0 && aclMyDefineCount == 0)) {
    this->destroyChain(_checkers + ACL_FILED_INDEX_WHOLE_METHOD_IP);
  }

  return 0;
}

int ACLMethodIpCheckList::check(const ACLContext & context)
{
  int action1 = ACLCheckList::check(_checkers[ACL_FILED_INDEX_METHOD].head, context);
  if (action1 == ACL_ACTION_DENY_INT) {
    return action1;
  }

  int action2 = ACLCheckList::check(_checkers[ACL_FILED_INDEX_SRC_IP].head, context);
  if (action2 == ACL_ACTION_DENY_INT) {
    return action2;
  }

  if (action1 != ACL_ACTION_NONE_INT) {
    return action1;
  }
  if (action2 != ACL_ACTION_NONE_INT) {
    return action2;
  }

  return ACLCheckList::check(_checkers[ACL_FILED_INDEX_WHOLE_METHOD_IP].head, context);
}

