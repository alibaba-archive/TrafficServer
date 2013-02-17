#ifndef _ACL_DEFINE_MANAGER_H
#define _ACL_DEFINE_MANAGER_H

#include "ACLDefineChecker.h"
#include "ACLParams.h"
#include "MappingTypes.h"

class ACLDefineManager {
  private:
    ACLDefineManager();

  public:
    ~ACLDefineManager();

    int init(const DirectiveParams *rootParams);

    inline ACLDefineChecker *find(const StringValue *svName) {
      char aclName[svName->length + 1];
      memcpy(aclName, svName->str, svName->length);
      *(aclName + svName->length) = '\0';
      return this->find(aclName);
    }

    ACLDefineChecker *find(const char *aclName);

    inline int count() const {
      return _defineCheckers != NULL ? _defineCheckers->count : 0;
    }

    static inline ACLDefineManager *getInstance() {
      return &_defineManager;
    }

    void print();

    //return old define checker should be deleted delay
    DynamicArray<ACLDefineChecker *> *commit();
    void rollback();

    static void freeDefineCheckers(DynamicArray<ACLDefineChecker *>
        *defineCheckers);

  protected:

    DynamicArray<ACLDefineChecker *> *_defineCheckers; //sort by aclName ASC
    DynamicArray<ACLDefineChecker *> *_oldDefineCheckers;
    static ACLDefineManager _defineManager;
};

#endif

