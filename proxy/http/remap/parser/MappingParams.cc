#include "MappingParams.h"

MappingParams::MappingParams(const int rank, const char *filename, const int lineNo,
    const char *lineStr, const int lineLen, DirectiveParams *parent,
    RemapDirective *directive, const char *paramStr,
    const int paramLen, const bool bBlock) :
  DirectiveParams(rank, filename, lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock), _type(MAPPING_TYPE_NONE),
      _flags(MAPPING_FLAG_NONE)
{
}


const char *MappingParams::toString(char *buff, int *len)
{
  char optionStr[256];
  this->getOptionStr(_flags, optionStr);
  if (*optionStr == '\0') {
    *len = sprintf(buff, "%s", _directive->getName());
  }
  else {
    *len = sprintf(buff, "%s %s", _directive->getName(), optionStr);
  }

  *len += sprintf(buff + *len, " %.*s %.*s",
      _urls[0].length, _urls[0].str, _urls[1].length, _urls[1].str);

  return (const char *)buff;
}

MapParams::MapParams(const int rank, const char *filename, const int lineNo,
    const char *lineStr, const int lineLen, DirectiveParams *parent,
    RemapDirective *directive, const char *paramStr,
    const int paramLen, const bool bBlock) :
  MappingParams(rank, filename, lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock)
{
  _type = MAPPING_TYPE_MAP;
}

int MapParams::parse(const char * /*blockStart*/, const char * /*blockEnd*/)
{
  int startIndex;
  if (_params[0].equals(MAP_OPTION_WITH_RECV_PORT,
        sizeof(MAP_OPTION_WITH_RECV_PORT) - 1))
  {
    _flags = MAP_FLAG_WITH_RECV_PORT;
    startIndex = 1;
  }
  else if (_params[0].equals(MAP_OPTION_REVERSE,
        sizeof(MAP_OPTION_REVERSE) - 1))
  {
    _flags = MAP_FLAG_REVERSE;
    startIndex = 1;
  }
  else {
    startIndex = 0;
  }

  if (_paramCount < startIndex + 2) {
    fprintf(stderr, "config file: %s, " \
        "invalid map format! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  if (_params[startIndex].equals(MAPPING_OPTION_PATH_REGEX,
        sizeof(MAPPING_OPTION_PATH_REGEX) - 1))
  {
    _flags |= MAPPING_FLAG_PATH_REGEX;
    startIndex++;
  }

  if (_paramCount != startIndex + 2) {
    fprintf(stderr, "config file: %s, " \
        "invalid map format! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  _urls[0] = _params[startIndex];
  _urls[1] = _params[startIndex + 1];
  return 0;
}

RedirectParams::RedirectParams(const int rank, const char *filename, const int lineNo,
    const char *lineStr, const int lineLen, DirectiveParams *parent,
    RemapDirective *directive, const char *paramStr,
    const int paramLen, const bool bBlock) :
  MappingParams(rank, filename, lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock)
{
  _type = MAPPING_TYPE_REDIRECT;
}

int RedirectParams::parse(const char * /*blockStart*/, const char * /*blockEnd*/)
{
  int startIndex;
  if (_params[0].equals(REDIRECT_OPTION_TEMPORARY,
        sizeof(REDIRECT_OPTION_TEMPORARY) - 1))
  {
    _flags = REDIRECT_FALG_TEMPORARY;
    startIndex = 1;
  }
  else {
    startIndex = 0;
  }

  if (_paramCount < startIndex + 2) {
    fprintf(stderr, "config file: %s, " \
        "invalid map format! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  if (_params[startIndex].equals(MAPPING_OPTION_PATH_REGEX,
        sizeof(MAPPING_OPTION_PATH_REGEX) - 1))
  {
    _flags |= MAPPING_FLAG_PATH_REGEX;
    startIndex++;
  }

  if (_paramCount != startIndex + 2) {
    fprintf(stderr, "config file: %s, " \
        "invalid redirect format! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  _urls[0] = _params[startIndex];
  _urls[1] = _params[startIndex + 1];
  return 0;
}

