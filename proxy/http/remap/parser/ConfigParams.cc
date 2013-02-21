#include "ConfigParams.h"

ConfigSetParams::ConfigSetParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock) :
  DirectiveParams(lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock)
{
}

int ConfigSetParams::parseKV(StringValue *sv)
{
  const char *pEqual = (const char *)memchr(sv->str, '=', sv->length);
  if (pEqual == NULL) {
    fprintf(stderr, "file: "__FILE__", line: %d, " \
        "invalid config setting: %.*s! config line no: %d, line: %.*s\n",
        __LINE__, sv->length, sv->str, _lineInfo.lineNo,
        _lineInfo.line.length, _lineInfo.line.str);
    return EINVAL;
  }

  _config.key.str = sv->str;
  _config.key.length = pEqual - sv->str;

  _config.value.str = pEqual + 1;
  _config.value.length = (sv->str + sv->length) - _config.value.str;
  if (_config.value.length == 0) {
    fprintf(stderr, "file: "__FILE__", line: %d, " \
        "invalid config setting: %.*s! config line no: %d, line: %.*s\n",
        __LINE__, sv->length, sv->str, _lineInfo.lineNo,
        _lineInfo.line.length, _lineInfo.line.str);
    return EINVAL;
  }

  return 0;
}

int ConfigSetParams::parse(const char *blockStat, const char *blockEnd)
{
  return this->parseKV(&_params[0]);
}

const char *ConfigSetParams::toString(char *buff, int *len)
{
  *len = sprintf(buff, "%s %.*s=%.*s", _directive->getName(),
      _config.key.length, _config.key.str, _config.value.length, _config.value.str);
  return (const char *)buff;
}

ConfigParams::ConfigParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock) :
  ConfigSetParams(lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock)
{
}

int ConfigParams::parse(const char *blockStat, const char *blockEnd)
{
  StringValue *svType = &_params[0];
  _config_type = this->getConfigType(svType);
  if (_config_type == CONFIG_TYPE_NONE) {
    fprintf(stderr, "file: "__FILE__", line: %d, " \
        "invalid config type: %.*s! config line no: %d, line: %.*s\n",
        __LINE__, svType->length, svType->str, _lineInfo.lineNo,
        _lineInfo.line.length, _lineInfo.line.str);
    return EINVAL;
  }

  if (_bBlock) {
    if (_paramCount != 1) {
      fprintf(stderr, "file: "__FILE__", line: %d, " \
          "invalid config format! config line no: %d, line: %.*s\n",
          __LINE__, _lineInfo.lineNo, _lineInfo.line.length,
          _lineInfo.line.str);
      return EINVAL;
    }

    _config.key.str = NULL;
    _config.key.length = 0;
    _config.value.str = NULL;
    _config.value.length = 0;
    return 0;
  }
  else {
    if (_paramCount != 2) {
      fprintf(stderr, "file: "__FILE__", line: %d, " \
          "invalid config format! config line no: %d, line: %.*s\n",
          __LINE__, _lineInfo.lineNo, _lineInfo.line.length,
          _lineInfo.line.str);
      return EINVAL;
    }

    return this->parseKV(&_params[1]);
  }
}

const char *ConfigParams::toString(char *buff, int *len)
{
  *len = sprintf(buff, "%s %s", _directive->getName(), getTypeCaption());
  if (_config.key.str != NULL && _config.value.str != NULL) {
    *len += sprintf(buff + *len, " %.*s=%.*s",
        _config.key.length, _config.key.str,
        _config.value.length, _config.value.str);
  }

  return (const char *)buff;
}

