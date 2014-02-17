#ifndef _ACL_REFERER_CHECK_LIST_H
#define _ACL_REFERER_CHECK_LIST_H

#include "ACLChecker.h"
#include "ACLDefineChecker.h"
#include "DirectiveParams.h"
#include "ACLCheckList.h"

#define ACL_FILED_INDEX_REFERER         0

class ACLRefererCheckList : public ACLCheckList {
  public:
    ACLRefererCheckList();
    ~ACLRefererCheckList() {}

    int load(const DirectiveParams *parentParams);

    inline int check(const ACLContext & context)
    {
      return ACLCheckList::check(_checkers[ACL_FILED_INDEX_REFERER].head, context);
    }

    bool needCheckHost() const;

    const char *getRedirectUrl() const {
      if (*_redirectUrl != '\0') {
        return _redirectUrl;
      }

      const char *redirectUrl;
      const char *result;
      ACLDefineChecker *defineChecker;

      result = NULL;
      ACLChecker *checker = _checkers[ACL_FILED_INDEX_REFERER].head;
      while (checker != NULL) {
        if ((defineChecker=dynamic_cast<ACLDefineChecker *>(checker)) != NULL) {
          if (defineChecker->isRefererChecker()) {
            redirectUrl = defineChecker->getRedirectUrl();
            if (*redirectUrl != '\0') {
              result = redirectUrl;
            }
          }
        }

        checker = checker->next();
      }

      return (result != NULL) ? result : _redirectUrl;
    }

  protected:
    char _redirectUrl[2 * 1024];
};

#endif

