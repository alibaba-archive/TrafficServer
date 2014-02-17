#include "ACLDefineManager.h"
#include "ACLChecker.h"
#include "ACLRefererCheckList.h"

ACLRefererCheckList::ACLRefererCheckList() : ACLCheckList()
{
  _checkerCount = 1;
}

int ACLRefererCheckList::load(const DirectiveParams *parentParams)
{
  const ACLRedirectUrlParams *redirectUrlParams;
  const ACLActionParams *actionParams;
  const ACLCheckParams *checkParams;

  const DirectiveParams *params;
  const ACLRefererParams *refererParams;
  ACLChecker *lastChecker = NULL;
  int aclTotalDefineCount = 0;
  int aclMyDefineCount = 0;
  int aclAnyCount = 0;

  const DirectiveParams *children = parentParams->getChildren();
  while (children != NULL) {
    if ((redirectUrlParams=dynamic_cast<const ACLRedirectUrlParams *>
          (children)) != NULL)
    {
      snprintf(_redirectUrl, sizeof(_redirectUrl), "%.*s",
          redirectUrlParams->getUrl()->length,
          redirectUrlParams->getUrl()->str);
      lastChecker = NULL;
    }
    else if ((checkParams=dynamic_cast<const ACLCheckParams *>
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
      if (((ACLDefineChecker *)lastChecker)->isRefererChecker()) {
        this->addChecker(_checkers + ACL_FILED_INDEX_REFERER, lastChecker);
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
        this->addChecker(_checkers + ACL_FILED_INDEX_REFERER, lastChecker);
        aclAnyCount++;
      }
      else if ((refererParams=dynamic_cast<const ACLRefererParams *>
            (params)) != NULL)
      {
        ACLRefererChecker *refererChecker;
        if (lastChecker == NULL || !((refererChecker=dynamic_cast<
                ACLRefererChecker *>(lastChecker)) != NULL &&
              refererChecker->getAction() == action))
        {
          refererChecker = new ACLRefererChecker(action);
          if (refererChecker == NULL) {
            return ENOMEM;
          }

          this->addChecker(_checkers + ACL_FILED_INDEX_REFERER,
              refererChecker);
        }

        refererChecker->add(refererParams);
        lastChecker = refererChecker;
      }
    }

    children = children->next();
  }

  if (aclAnyCount > 0 && (aclTotalDefineCount > 0 && aclMyDefineCount == 0)) {
    ACLChecker *previous = NULL;
    ACLChecker *current;
    ACLChecker *checker;

    checker = _checkers[ACL_FILED_INDEX_REFERER].head;
    _checkers[ACL_FILED_INDEX_REFERER].head = NULL;
    _checkers[ACL_FILED_INDEX_REFERER].tail = NULL;
    while (checker != NULL) {
      current = checker;
      checker = checker->_next;

      if (dynamic_cast<const ACLAllChecker *>(current) != NULL) {
        if (previous != NULL) {
          previous->_next = checker;
        }
        delete current;
      }
      else {
        if (_checkers[ACL_FILED_INDEX_REFERER].head == NULL) {
          _checkers[ACL_FILED_INDEX_REFERER].head = current;
        }
        _checkers[ACL_FILED_INDEX_REFERER].tail = current;
        previous = current;
      }
    }
  }

  return 0;
}

bool ACLRefererCheckList::needCheckHost() const
{
  ACLRefererChecker *refererChecker;
  ACLDefineChecker *defineChecker;
  ACLChecker *checker = _checkers[ACL_FILED_INDEX_REFERER].head;
  while (checker != NULL) {
    if (dynamic_cast<ACLRefererHostChecker *>(checker) != NULL) {
      return true;
    }
    if ((refererChecker=dynamic_cast<ACLRefererChecker *>(checker)) != NULL) {
      if (refererChecker->needCheckRefererHost()) {
        return true;
      }
    }
    if ((defineChecker=dynamic_cast<ACLDefineChecker *>(checker)) != NULL) {
      if (defineChecker->needCheckRefererHost()) {
        return true;
      }
    }

    checker = checker->_next;
  }

  return false;
}

