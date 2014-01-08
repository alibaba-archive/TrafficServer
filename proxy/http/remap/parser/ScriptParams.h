#ifndef _SCRIPT_PARAMS_H
#define _SCRIPT_PARAMS_H

#include <string.h>
#include "MappingTypes.h"
#include "DirectiveParams.h"

#define LUA_DIRECTIVE_STR "lua"
#define LUA_DIRECTIVE_LEN (sizeof(LUA_DIRECTIVE_STR) - 1)

#define PHASE_NAME_DO_REMAP              "do_remap"
#define PHASE_NAME_SEND_REQUEST          "send_request"
#define PHASE_NAME_READ_RESPONSE         "read_response"
#define PHASE_NAME_SEND_RESPONSE         "send_response"
#define PHASE_NAME_CACHE_LOOKUP_COMPLETE "cache_lookup_complete"

#define LUA_PLUGIN_FILENAME "/usr/lib64/trafficserver/plugins/libtslua.so"

class ScriptParams : public DirectiveParams {
  public:
    ScriptParams(const int rank, const char *filename, const int lineNo,
        const char *lineStr, const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);
    ~ScriptParams() {}
    int parse(const char *blockStart, const char *blockEnd);

    inline StringValue & getLanguage() {
      return _language;
    }

    inline StringValue & getPhase() {
      return _phase;
    }

    inline bool isDoRemapPhase() const {
      return (_phase.length == 0 || _phase.equals(PHASE_NAME_DO_REMAP,
            sizeof(PHASE_NAME_DO_REMAP) - 1));
    }

    const char *getHookPoint() const {
      if (isDoRemapPhase()) {
        return NULL;
      }

      if (_phase.equals(PHASE_NAME_SEND_REQUEST,
            sizeof(PHASE_NAME_SEND_REQUEST) - 1))
      {
        return "TS_LUA_HOOK_SEND_REQUEST_HDR";
      }
      else if (_phase.equals(PHASE_NAME_READ_RESPONSE,
            sizeof(PHASE_NAME_READ_RESPONSE) - 1))
      {
        return "TS_LUA_HOOK_READ_RESPONSE_HDR";
      }
      else if (_phase.equals(PHASE_NAME_SEND_RESPONSE,
            sizeof(PHASE_NAME_SEND_RESPONSE) - 1))
      {
        return "TS_LUA_HOOK_SEND_RESPONSE_HDR";
      }
      else if (_phase.equals(PHASE_NAME_CACHE_LOOKUP_COMPLETE,
            sizeof(PHASE_NAME_CACHE_LOOKUP_COMPLETE) - 1))
      {
        return "TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE";
      }
      else {
        return NULL;
      }
    }

    bool languageEquals(const StringValue & language) const;

    bool phaseEquals(const StringValue & phase) const;

    static bool checkScriptPhases(const ScriptParams **scriptParamsArray,
        const int scriptCount);

    static int generateScript(const ScriptParams **scriptParamsArray,
        const int scriptCount, StringValue *script);

  protected:
     const char *toString(char *buff, int *len);
     StringValue _language;
     StringValue _phase;
     StringValue _script;
};

#endif
