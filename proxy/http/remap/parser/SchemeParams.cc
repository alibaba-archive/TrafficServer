#include "SchemeParams.h"

SchemeParams::SchemeParams(const int rank, const char *filename, const int lineNo,
    const char *lineStr, const int lineLen, DirectiveParams *parent,
    RemapDirective *directive, const char *paramStr,
    const int paramLen, const bool bBlock) :
  DirectiveParams(rank, filename, lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock)
{
}

int SchemeParams::parse(const char *blockStart, const char *blockEnd)
{
  _host = _params[0];
  return 0;
}

const char *SchemeParams::toString(char *buff, int *len)
{
  *len = sprintf(buff, "%s %.*s", _directive->getName(),
      _host.length, _host.str);

  return (const char *)buff;
}

