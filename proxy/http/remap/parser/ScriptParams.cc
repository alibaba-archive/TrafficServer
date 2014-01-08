#include "ScriptParams.h"
#include "ts/ink_base64.h"

ScriptParams::ScriptParams(const int rank, const char *filename, const int lineNo,
    const char *lineStr, const int lineLen, DirectiveParams *parent,
    RemapDirective *directive, const char *paramStr,
    const int paramLen, const bool bBlock) :
  DirectiveParams(rank, filename, lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock)
{
}

int ScriptParams::parse(const char *blockStart, const char *blockEnd)
{
#define PHASE_COUNT 5
  int index;
  int i;
  const char *phases[PHASE_COUNT] = {
    PHASE_NAME_DO_REMAP,
    PHASE_NAME_SEND_REQUEST,
    PHASE_NAME_READ_RESPONSE,
    PHASE_NAME_SEND_RESPONSE,
    PHASE_NAME_CACHE_LOOKUP_COMPLETE
  };

  _script.str = blockStart + 1;
  _script.length = blockEnd - 1 - _script.str;

  if (_paramCount == 0) {
    return 0;
  }

  index = 0;
  if (_params[0].equals(LUA_DIRECTIVE_STR, LUA_DIRECTIVE_LEN)) {
    _language = _params[0];
    index++;
    if (_paramCount - index == 0) {
      return 0;
    }
  }

  if (_paramCount - index > 1) {
    fprintf(stderr, "config file: %s, " \
        "invalid script format! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  for (i=0; i<PHASE_COUNT; i++) {
     if (_params[index].equals(phases[i], strlen(phases[i]))) {
       _phase = _params[index];
       break;
     }
  }

  if (i == PHASE_COUNT) {
    fprintf(stderr, "config file: %s, " \
        "invalid phase string: %.*s! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _params[index].length, _params[index].str,
        _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  return 0;
}

const char *ScriptParams::toString(char *buff, int *len)
{
  *len = sprintf(buff, "%s", _directive->getName());
  if (_language.length > 0) {
    *len += sprintf(buff + *len, " %.*s", _language.length, _language.str);
  }
  if (_phase.length > 0) {
    *len += sprintf(buff + *len, " %.*s", _phase.length, _phase.str);
  }
  *len += sprintf(buff + *len, " {\n%.*s}\n", _script.length, _script.str);

  return (const char *)buff;
}

bool ScriptParams::phaseEquals(const StringValue & phase) const
{
  if (_phase.length == 0) {
    if (phase.length == 0) {
      return true;
    }
    else {
      return phase.equals(PHASE_NAME_DO_REMAP, sizeof(PHASE_NAME_DO_REMAP) - 1);
    }
  }
  else {
    if (phase.length == 0) {
      return _phase.equals(PHASE_NAME_DO_REMAP, sizeof(PHASE_NAME_DO_REMAP) - 1);
    }
    else {
      return _phase.equals(&phase);
    }
  }
}

bool ScriptParams::languageEquals(const StringValue & language) const
{
  if (_language.length == 0) {
    if (language.length == 0) {
      return true;
    }
    else {
      return language.equals(LUA_DIRECTIVE_STR, LUA_DIRECTIVE_LEN);
    }
  }
  else {
    if (language.length == 0) {
      return _language.equals(LUA_DIRECTIVE_STR, LUA_DIRECTIVE_LEN);
    }
    else {
      return _language.equals(&language);
    }
  }
}

bool ScriptParams::checkScriptPhases(const ScriptParams **scriptParamsArray,
    const int scriptCount)
{
  int i, k;
  StringValue sv;

  if (scriptCount == 1) {
    return true;
  }

  for (i=0; i<scriptCount; i++) {
    for (k=i+1; k<scriptCount; k++) {
      if (scriptParamsArray[i]->phaseEquals(scriptParamsArray[k]->_phase)) {
        sv = scriptParamsArray[k]->_phase;
        if (sv.length == 0) {
          sv.str = PHASE_NAME_DO_REMAP;
          sv.length = sizeof(PHASE_NAME_DO_REMAP) - 1;
        }
        fprintf(stderr, "config file: %s, " \
            "phase: %.*s duplicate! config line no: %d, line: %.*s\n",
            scriptParamsArray[k]->_lineInfo.filename, sv.length, sv.str,
            scriptParamsArray[k]->_lineInfo.lineNo,
            scriptParamsArray[k]->_lineInfo.line.length,
            scriptParamsArray[k]->_lineInfo.line.str);
        return false;
      }
    }
  }

  return true;
}

int ScriptParams::generateScript(const ScriptParams **scriptParamsArray,
    const int scriptCount, StringValue *script)
{
  char *buff;
  char *p;
  int allocSize;
  const char *hookPoint;
  const ScriptParams **ppScriptParams;
  const ScriptParams **scriptParamsEnd;
  const ScriptParams *doRemapScriptParams;

  allocSize = 1024;
  scriptParamsEnd = scriptParamsArray + scriptCount;
  for (ppScriptParams=scriptParamsArray; ppScriptParams<scriptParamsEnd;
      ppScriptParams++)
  {
    allocSize += (*ppScriptParams)->_script.length;
  }

  buff = (char *)malloc(allocSize);
  if (buff == NULL) {
    fprintf(stderr, "malloc %d bytes fail", allocSize);
    return ENOMEM;
  }

  p = buff;
  for (ppScriptParams=scriptParamsArray; ppScriptParams<scriptParamsEnd;
      ppScriptParams++)
  {
    if ((*ppScriptParams)->isDoRemapPhase()) {
      continue;
    }

    p += sprintf(p, "function %.*s()\n%.*s\nend\n",
        (*ppScriptParams)->_phase.length, (*ppScriptParams)->_phase.str,
        (*ppScriptParams)->_script.length, (*ppScriptParams)->_script.str);
  }

  p += sprintf(p, "function do_remap()\n");

  doRemapScriptParams = NULL;
  for (ppScriptParams=scriptParamsArray; ppScriptParams<scriptParamsEnd;
      ppScriptParams++)
  {
    if ((*ppScriptParams)->isDoRemapPhase()) {
      doRemapScriptParams = *ppScriptParams;
      continue;
    }

    hookPoint = (*ppScriptParams)->getHookPoint();
    p += sprintf(p, "    ts.hook(%s, %.*s)\n", hookPoint,
        (*ppScriptParams)->_phase.length, (*ppScriptParams)->_phase.str);
  }

  if (doRemapScriptParams != NULL) {
    p += sprintf(p, "%.*s\n", doRemapScriptParams->_script.length,
        doRemapScriptParams->_script.str);
  }

  p += sprintf(p, "end\n");

  script->str = buff;
  script->length = p - buff;
  return 0;
}

