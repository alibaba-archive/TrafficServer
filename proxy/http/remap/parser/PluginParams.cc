#include "PluginParams.h"

PluginParams::PluginParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock) :
  DirectiveParams(lineNo, lineStr, lineLen, parent, directive, 
      paramStr, paramLen, bBlock)
{
}

int PluginParams::parse()
{
  _pluginInfo.filename = _params[0];
  _pluginInfo.paramCount = _paramCount - 1;
  for (int i=1; i<_paramCount; i++) {
     _pluginInfo.params[i - 1] = _params[i];
  }

  return 0;
}

const char *PluginParams::toString(char *buff, int *len)
{
  *len = sprintf(buff, "%s %.*s", _directive->getName(), 
      _pluginInfo.filename.length, _pluginInfo.filename.str);

  for (int i=0; i<_pluginInfo.paramCount; i++) {
    if (memchr(_pluginInfo.params[i].str, ' ', _pluginInfo.params[i].length) != NULL) {
      *len += sprintf(buff + *len, " \"%.*s\"", 
          _pluginInfo.params[i].length, _pluginInfo.params[i].str);
    }
    else {
      *len += sprintf(buff + *len, " %.*s", 
          _pluginInfo.params[i].length, _pluginInfo.params[i].str);
    }
  }

  return (const char *)buff;
}

