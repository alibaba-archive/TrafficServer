#include "ACLDefineManager.h"
#include "ACLCheckList.h"

ACLCheckList::ACLCheckList() : _checkerCount(0)
{
}

ACLCheckList::~ACLCheckList()
{
  for (int i=0; i<_checkerCount; i++) {
    if (_checkers[i].head == NULL) {
      continue;
    }

    this->destroyChain(_checkers + i);
  }
}

void ACLCheckList::destroyChain(ACLCheckerChain *checkersChain)
{
  ACLChecker *forDelete;
  ACLChecker *checkers;

  checkers = checkersChain->head;
  while (checkers != NULL) {
    forDelete = checkers;
    checkers = checkers->_next;

    if (dynamic_cast<ACLDefineChecker *>(forDelete) == NULL) {
      delete forDelete;
    }
  }

  checkersChain->head = NULL;
  checkersChain->tail = NULL;
}

