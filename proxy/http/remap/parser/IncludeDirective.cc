#include "IncludeDirective.h"
#include "IncludeParams.h"

IncludeDirective::IncludeDirective() :
  RemapDirective(DIRECTVIE_NAME_INCLUDE, DIRECTIVE_TYPE_STATEMENT, 1, 1)
{
}

IncludeDirective::~IncludeDirective()
{
}

DirectiveParams *IncludeDirective::newDirectiveParams(const int rank, const char *filename,
    const int lineNo, const char *lineStr, const int lineLen,
    DirectiveParams *parent, const char *paramStr, const int paramLen,
    const bool bBlock)
{
  return new IncludeParams(rank, filename, lineNo, lineStr, lineLen, parent,
      this, paramStr, paramLen, bBlock);
}

