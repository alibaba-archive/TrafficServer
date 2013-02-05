#include "MappingDirective.h"
#include "ACLDirective.h"
#include "ConfigDirective.h"
#include "PluginDirective.h"
#include "MappingParams.h"

MappingDirective::MappingDirective(const char *name) : 
  RemapDirective(name, DIRECTIVE_TYPE_BOTH, 2, 3)
{
  int index = 0;
  this->_children[index++] = new ACLDirective();
  this->_children[index++] = new ConfigDirective();
  this->_children[index++] = new PluginDirective();
  this->_childrenCount = index;
}


MapDirective::MapDirective() : MappingDirective(DIRECTVIE_NAME_MAP)
{
}

DirectiveParams *MapDirective::newDirectiveParams(const int lineNo, 
    const char *lineStr, const int lineLen, DirectiveParams *parent, 
    const char *paramStr, const int paramLen, const bool bBlock)

{
  return new MapParams(lineNo, lineStr, lineLen, parent, this, 
      paramStr, paramLen, bBlock);
}

RedirectDirective::RedirectDirective() : MappingDirective(DIRECTVIE_NAME_REDIRECT)
{
}

DirectiveParams *RedirectDirective::newDirectiveParams(const int lineNo, 
    const char *lineStr, const int lineLen, DirectiveParams *parent, 
    const char *paramStr, const int paramLen, const bool bBlock)
{
  return new RedirectParams(lineNo, lineStr, lineLen, parent, this, 
      paramStr, paramLen, bBlock);
}

