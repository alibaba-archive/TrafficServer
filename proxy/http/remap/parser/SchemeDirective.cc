#include "SchemeDirective.h"
#include "ACLDirective.h"
#include "MappingDirective.h"
#include "SchemeParams.h"

SchemeDirective::SchemeDirective(const char *name) :
  RemapDirective(name, DIRECTIVE_TYPE_BLOCK, 1, 1)
{
  int index = 0;
  this->_children[index++] = new ACLDirective();
  this->_children[index++] = new MapDirective();
  this->_children[index++] = new RedirectDirective();
  this->_childrenCount = index;
}

SchemeDirective::~SchemeDirective()
{
}

DirectiveParams *SchemeDirective::newDirectiveParams(const int rank, const char *filename,
    const int lineNo, const char *lineStr, const int lineLen,
    DirectiveParams *parent, const char *paramStr, const int paramLen,
    const bool bBlock)
{
  return new SchemeParams(rank, filename, lineNo, lineStr, lineLen, parent,
      this, paramStr, paramLen, bBlock);
}

