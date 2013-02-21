#include "PluginDirective.h"
#include "PluginParams.h"

PluginDirective::PluginDirective() :
  RemapDirective(DIRECTVIE_NAME_PLUGIN, DIRECTIVE_TYPE_BOTH, 1, 0)
{
  this->_children[0] = new PluginParamDirective();
  this->_childrenCount = 1;
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

PluginParamDirective::PluginParamDirective() :
  RemapDirective(DIRECTVIE_NAME_PLUGIN_PARAM, DIRECTIVE_TYPE_BOTH, 0, 2)
{
}

PluginParamDirective::~PluginParamDirective()
{
}

DirectiveParams *PluginParamDirective::newDirectiveParams(const int lineNo,
    const char *lineStr, const int lineLen, DirectiveParams *parent,
    const char *paramStr, const int paramLen, const bool bBlock)
{
  return new PluginParamParams(lineNo, lineStr, lineLen, parent, this,
      paramStr, paramLen, bBlock);
}

