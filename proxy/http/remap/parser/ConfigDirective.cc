#include "ConfigDirective.h"
#include "ConfigParams.h"

ConfigDirective::ConfigDirective() : 
  RemapDirective(DIRECTVIE_NAME_CONFIG, DIRECTIVE_TYPE_BOTH, 1, 2)
{
  this->_children[0] = new ConfigSetDirective();
  this->_childrenCount = 1;
}

ConfigDirective::~ConfigDirective()
{
}


DirectiveParams *ConfigDirective::newDirectiveParams(const int lineNo, 
    const char *lineStr, const int lineLen, DirectiveParams *parent, 
    const char *paramStr, const int paramLen, const bool bBlock)
{
  return new ConfigParams(lineNo, lineStr, lineLen, parent, this, 
      paramStr, paramLen, bBlock);
}


ConfigSetDirective::ConfigSetDirective() : 
  RemapDirective(DIRECTVIE_NAME_CONFIG_SET, DIRECTIVE_TYPE_STATEMENT, 1, 1)
{
}

ConfigSetDirective::~ConfigSetDirective()
{
}

DirectiveParams *ConfigSetDirective::newDirectiveParams(const int lineNo, 
    const char *lineStr, const int lineLen, DirectiveParams *parent, 
    const char *paramStr, const int paramLen, const bool bBlock)
{
  return new ConfigSetParams(lineNo, lineStr, lineLen, parent, this, 
      paramStr, paramLen, bBlock);
}

