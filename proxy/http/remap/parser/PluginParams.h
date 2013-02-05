#ifndef _PLUGIN_PARAMS_H
#define _PLUGIN_PARAMS_H

#include <string.h>
#include "MappingTypes.h"
#include "DirectiveParams.h"

class PluginParams : public DirectiveParams {
  public:
    PluginParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);
    ~PluginParams() {}
    int parse();

    const PluginInfo *getPluginInfo() const {
      return &_pluginInfo;
    }

  protected:
     const char *toString(char *buff, int *len);
     PluginInfo _pluginInfo;
};

#endif

