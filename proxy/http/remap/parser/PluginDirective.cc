#include "PluginDirective.h"
#include "PluginParams.h"

PluginDirective::PluginDirective() : 
  RemapDirective(DIRECTVIE_NAME_PLUGIN, DIRECTIVE_TYPE_STATEMENT, 1, 0)
{
}

PluginDirective::~PluginDirective()
{
}

DirectiveParams *PluginDirective::newDirectiveParams(const int lineNo, 
    const char *lineStr, const int lineLen, DirectiveParams *parent, 
    const char *paramStr, const int paramLen, const bool bBlock)
{
  return new PluginParams(lineNo, lineStr, lineLen, parent, this, 
      paramStr, paramLen, bBlock);
}

