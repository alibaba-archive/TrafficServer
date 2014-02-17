#ifndef _ACL_CHECK_LIST_H
#define _ACL_CHECK_LIST_H

#include "ACLChecker.h"
#include "ACLDefineChecker.h"
#include "DirectiveParams.h"

#define ACL_MAX_FIELD_COUNT  3

struct ACLCheckerChain {
  ACLChecker *head;
  ACLChecker *tail;

  ACLCheckerChain() : head(NULL), tail(NULL) {}
};

class ACLCheckList {
  protected:
    ACLCheckList();

  public:
    virtual ~ACLCheckList();

    inline bool empty() const {
      for (int i=0; i<_checkerCount; i++) {
        if (_checkers[i].head != NULL) {
          return false;
        }
      }
      return true;
    }

    void print() {
      for (int i=0; i<_checkerCount; i++) {
        ACLChecker *checker = _checkers[i].head;
        while (checker != NULL) {
          checker->print();
          checker = checker->next();
        }
      }
    }

  protected:
    void destroyChain(ACLCheckerChain *checkersChain);

    inline int check(ACLChecker *checker, const ACLContext & context)
    {
      while (checker != NULL) {
        if (checker->match(context)) {
          return checker->getAction();
        }

        checker = checker->next();
      }

      return ACL_ACTION_NONE_INT;
    }

    inline void addChecker(ACLCheckerChain *chain, ACLChecker *checker) {
      if (chain->head == NULL) {
        chain->head = checker;
      }
      else {
        chain->tail->_next = checker;
      }
      chain->tail = checker;
    }

    int _checkerCount;
    ACLCheckerChain _checkers[ACL_MAX_FIELD_COUNT];
};

#endif

