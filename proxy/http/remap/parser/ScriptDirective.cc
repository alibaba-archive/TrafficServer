#include "ScriptDirective.h"
#include "ScriptParams.h"

ScriptDirective::ScriptDirective() :
  RemapDirective(DIRECTVIE_NAME_SCRIPT, DIRECTIVE_TYPE_BLOCK, 0, 2)
{
}

ScriptDirective::~ScriptDirective()
{
}

DirectiveParams *ScriptDirective::newDirectiveParams(const int rank, const char *filename,
    const int lineNo, const char *lineStr, const int lineLen,
    DirectiveParams *parent, const char *paramStr, const int paramLen,
    const bool bBlock)
{
  return new ScriptParams(rank, filename, lineNo, lineStr, lineLen, parent, this,
      paramStr, paramLen, bBlock);
}

