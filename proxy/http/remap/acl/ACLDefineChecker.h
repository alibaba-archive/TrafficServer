#ifndef _ACL_DEFINE_CHECKER_H
#define _ACL_DEFINE_CHECKER_H

#include "ACLParams.h"
#include "ACLChecker.h"

class ACLDefineChecker : public ACLChecker {
  public:
    ACLDefineChecker();
    ~ACLDefineChecker();

    bool init(const ACLDefineParams *defineParams);
    bool match(const ACLContext & context);

    inline bool empty() const {
      return (_methodChecker == NULL && _srcIpChecker == NULL &&
          _refererChecker == NULL);
    }

    inline ACLDefineChecker * defineNext() const {
      return _defineNext;
    }

    inline void setAclName(const char *aclName) {
      snprintf(_aclName, sizeof(_aclName), "%s", aclName);
    }

    inline const char * getAclName() const {
      return _aclName;
    }

    inline const char *getRedirectUrl() const {
      return _redirectUrl;
    }

    bool isValid() const {
      if (this->empty()) {
        return false;
      }

      if (_refererChecker == NULL) {
        return true;
      }

      return (_methodChecker == NULL && _srcIpChecker == NULL);
    }

    inline bool isRefererChecker() const {
      return _refererChecker != NULL;
    }

    inline bool needCheckRefererHost() const {
      return _refererChecker != NULL &&
        _refererChecker->needCheckRefererHost();
    }

    void print(const char *prefix);

  protected:
    char _aclName[64];
    char _redirectUrl[2 * 1024];

    ACLMethodChecker *_methodChecker;
    ACLSrcIpChecker *_srcIpChecker;
    ACLRefererChecker *_refererChecker;

    ACLDefineChecker *_defineNext;  //for define chain, used by ACLDefineManager
};

#endif

