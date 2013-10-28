#ifndef _PLUGIN_PARAMS_H
#define _PLUGIN_PARAMS_H

#include <string.h>
#include "MappingTypes.h"
#include "DirectiveParams.h"

#define BASE64_DIRECTIVE_STR "base64"
#define BASE64_DIRECTIVE_LEN (sizeof(BASE64_DIRECTIVE_STR) - 1)

class PluginParams : public DirectiveParams {
  public:
    PluginParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);
    ~PluginParams() {}
    int parse(const char *blockStart, const char *blockEnd);

    int combineParams();

    const PluginInfo *getPluginInfo() const {
      return &_pluginInfo;
    }

  protected:
     const char *toString(char *buff, int *len);
     bool _paramsCombined;
     PluginInfo _pluginInfo;
};

class PluginParamParams : public DirectiveParams {
  public:
    PluginParamParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);

    ~PluginParamParams() {
      _paramValue.free();
    }

    int parse(const char *blockStart, const char *blockEnd);

    bool isBase64() const {
      return _base64;
    }

    const StringValue *getParamValue() const {
      return &_paramValue;
    }

  protected:
     const char *toString(char *buff, int *len);
     bool _base64;   //if base64 encoded
     StringValue _base64Value; //base64 encoded
     StringValue _paramValue;  //raw
};

#endif

