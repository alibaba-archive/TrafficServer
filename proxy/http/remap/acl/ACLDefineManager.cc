#include <stdlib.h>
#include "ACLDefineManager.h"

ACLDefineManager ACLDefineManager::_defineManager;

ACLDefineManager::ACLDefineManager() : _defineCheckers(NULL),
    _oldDefineCheckers(NULL)
{
}

ACLDefineManager::~ACLDefineManager()
{
  freeDefineCheckers(_defineCheckers);
}

DynamicArray<ACLDefineChecker *> *ACLDefineManager::commit()
{
  DynamicArray<ACLDefineChecker *> *oldCheckers;
  oldCheckers = _oldDefineCheckers;
  _oldDefineCheckers = NULL;
  return oldCheckers;
}

void ACLDefineManager::rollback()
{
  if (_oldDefineCheckers == NULL) {
    return;
  }

  freeDefineCheckers(_defineCheckers);
  _defineCheckers = _oldDefineCheckers;
  _oldDefineCheckers = NULL;
}

void ACLDefineManager::freeDefineCheckers(
    DynamicArray<ACLDefineChecker *> *defineCheckers)
{
  if (defineCheckers == NULL) {
    return;
  }

  for (int i=0; i<defineCheckers->count; i++) {
    delete defineCheckers->items[i];
  }

  delete defineCheckers;
  defineCheckers = NULL;
}

void ACLDefineManager::print()
{
  if (_defineCheckers == NULL) {
    return;
  }

  printf("acl define count: %d\n", _defineCheckers->count);
  for (int i=0; i<_defineCheckers->count; i++) {
    _defineCheckers->items[i]->print("");
    printf("\n");
  }
}

static int ACLNameCompare(const void *p1, const void *p2)
{
  return strcmp((*((ACLDefineChecker **)p1))->getAclName(),
      (*((ACLDefineChecker **)p2))->getAclName());
}

ACLDefineChecker *ACLDefineManager::find(const char *aclName)
{
  ACLDefineChecker targetChecker;
  ACLDefineChecker *pTargetChecker;
  ACLDefineChecker **ppFound;

  if (_defineCheckers == NULL || _defineCheckers->items == NULL) {
    return NULL;
  }

  targetChecker.setAclName(aclName);
  pTargetChecker = &targetChecker;
  ppFound = (ACLDefineChecker **)bsearch(&pTargetChecker, _defineCheckers->items,
      _defineCheckers->count, sizeof(ACLDefineChecker *), ACLNameCompare);
  if (ppFound == NULL) {
    return NULL;
  }
  else {
    return *ppFound;
  }
}

int ACLDefineManager::init(const DirectiveParams *rootParams)
{
  if (_oldDefineCheckers != NULL) {
    fprintf(stderr, "file: "__FILE__", line: %d, already in progress!\n",
        __LINE__);
    return EINPROGRESS;
  }

  _oldDefineCheckers = _defineCheckers;
  _defineCheckers = new DynamicArray<ACLDefineChecker *>(32);

  const ACLDefineParams *defineParams;
  ACLDefineChecker *defineChecker;
  const DirectiveParams *children = rootParams->getChildren();
  while (children != NULL) {
    defineParams = dynamic_cast<const ACLDefineParams *>(children);
    if (defineParams != NULL) {
      defineChecker = new ACLDefineChecker();
      if (defineChecker == NULL) {
        return errno != 0 ? errno : ENOMEM;
      }

      if (!defineChecker->init(defineParams)) {
        delete defineChecker;
        return errno != 0 ? errno : ENOMEM;
      }

      if (defineChecker->empty()) {
        fprintf(stderr, "no acl rule, acl name: %s\n",
            defineChecker->getAclName());
        delete defineChecker;
        return ENOENT;
      }

      if (!defineChecker->isValid()) {
        fprintf(stderr, "acl rule is invalid, method / src_ip and referer "\
            "can't occur same time, acl name: %s\n",
            defineChecker->getAclName());
        delete defineChecker;
        return EINVAL;
      }

      if (defineChecker->isRefererChecker()) {
        if (*(defineChecker->getRedirectUrl()) == '\0') {
          fprintf(stderr, "Notice: no redirect_url, acl name: %s\n",
            defineChecker->getAclName());
        }
      }
      else {
        if (*(defineChecker->getRedirectUrl()) != '\0') {
          fprintf(stderr, "Notice: ignore redirect_url, acl name: %s\n",
            defineChecker->getAclName());
        }
      }

      if (this->find(defineChecker->getAclName()) != NULL) {
        fprintf(stderr, "duplicate acl name: %s\n",
            defineChecker->getAclName());

        delete defineChecker;
        return EEXIST;
      }

      if (!_defineCheckers->add(defineChecker)) {
        return errno != 0 ? errno : ENOMEM;
      }

      if (_defineCheckers->count > 1) {
        qsort(_defineCheckers->items, _defineCheckers->count, sizeof(ACLDefineChecker *),
            ACLNameCompare);
      }
    }

    children = children->next();
  }

  return 0;
}

