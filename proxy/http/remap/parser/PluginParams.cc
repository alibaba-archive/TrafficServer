#include "PluginParams.h"
#include "ts/ink_base64.h"

PluginParams::PluginParams(const int rank, const char *filename, const int lineNo,
    const char *lineStr, const int lineLen, DirectiveParams *parent,
    RemapDirective *directive, const char *paramStr,
    const int paramLen, const bool bBlock) :
  DirectiveParams(rank, filename, lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock), _paramsCombined(false)
{
}

int PluginParams::parse(const char *blockStart, const char *blockEnd)
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

int PluginParams::combineParams()
{
  if (_paramsCombined) {
    return 0;
  }

  const PluginParamParams *paramParams;
  DirectiveParams *child = this->_children.head;
  while (child != NULL) {
     if ((paramParams=dynamic_cast<const PluginParamParams *>(child)) != NULL) {
       if (_pluginInfo.paramCount >= MAX_PARAM_NUM - 1) {
         fprintf(stderr, "config file: %s, " \
             "config line no: %d, too many puglin parameters, exceeds: %d\n",
             _lineInfo.filename, _lineInfo.lineNo, MAX_PARAM_NUM - 1);
         return E2BIG;
       }

       _pluginInfo.params[_pluginInfo.paramCount++] = *(paramParams->getParamValue());
     }

     child = child->_next;
  }

  _paramsCombined = true;
  return 0;
}

PluginParamParams::PluginParamParams(const int rank, const char *filename,
    const int lineNo, const char *lineStr, const int lineLen,
    DirectiveParams *parent, RemapDirective *directive, const char *paramStr,
    const int paramLen, const bool bBlock) :
  DirectiveParams(rank, filename, lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock), _base64(false)
{
}

int PluginParamParams::parse(const char *blockStart, const char *blockEnd)
{
  if (_paramCount == 0) {
    if (!_bBlock) {
      fprintf(stderr, "config file: %s, " \
          "expect block statement! config line no: %d, line: %.*s\n",
          _lineInfo.filename, _lineInfo.lineNo, _lineInfo.line.length,
          _lineInfo.line.str);
      return EINVAL;
    }

    StringValue sbValue;
    _base64 = false;
    sbValue.str = blockStart + 1;
    sbValue.length = blockEnd - 1 - sbValue.str;
    sbValue.strdup(&_paramValue);
    return 0;
  }

  _base64 = _params[0].equals(BASE64_DIRECTIVE_STR, BASE64_DIRECTIVE_LEN);
  if (_paramCount == 1) {
    if (!_base64) {
      if (_bBlock) {
        fprintf(stderr, "config file: %s, " \
            "invalid block statement without base64 encode! " \
            "config line no: %d, line: %.*s\n",
            _lineInfo.filename, _lineInfo.lineNo,
            _lineInfo.line.length, _lineInfo.line.str);
        return EINVAL;
      }

      _params[0].strdup(&_paramValue);
      return 0;
    }

    if (!_bBlock) {
      fprintf(stderr, "config file: %s, " \
          "expect base64 encoded value! config line no: %d, line: %.*s\n",
          _lineInfo.filename, _lineInfo.lineNo, _lineInfo.line.length,
          _lineInfo.line.str);
      return EINVAL;
    }

    _base64Value.str = blockStart + 1;
    _base64Value.length = blockEnd - 1 - _base64Value.str;
  }
  else {  //_paramCount == 2
    if (_bBlock) {
      fprintf(stderr, "config file: %s, " \
          "invalid parameter count: %d with block statement! " \
          "config line no: %d, line: %.*s\n",
          _lineInfo.filename, _paramCount, _lineInfo.lineNo,
          _lineInfo.line.length, _lineInfo.line.str);
      return EINVAL;
    }

    if (!_base64) {
      fprintf(stderr, "config file: %s, " \
          "invalid parameter 1: %.*s, expect: %s! " \
          "config line no: %d, line: %.*s\n",
          _lineInfo.filename, _params[0].length, _params[0].str,
          BASE64_DIRECTIVE_STR, _lineInfo.lineNo,
          _lineInfo.line.length, _lineInfo.line.str);
      return EINVAL;
    }

    _base64Value = _params[1];
  }

  char *szFormattedBase64;
  szFormattedBase64 = (char *)malloc(_base64Value.length + 1);
  if (szFormattedBase64 == NULL) {
      fprintf(stderr, "config file: %s, " \
          "malloc %d bytes fail\n", _lineInfo.filename, _base64Value.length + 1);
      return ENOMEM;
  }

  char *pDest;
  const char *pSrc;
  const char *pEnd;
  pEnd = _base64Value.str + _base64Value.length;
  pDest = szFormattedBase64;
  for (pSrc=_base64Value.str; pSrc<pEnd; pSrc++) {
    if (*pSrc == ' ' || *pSrc == '\t' || *pSrc == '\r' || *pSrc == '\n') {
      continue;
    }

    *pDest++ = *pSrc;
  }

  int nFormattedBase64 = pDest - szFormattedBase64;
  int buffSize = ATS_BASE64_DECODE_DSTLEN(nFormattedBase64);
  _paramValue.str = (const char *)malloc(buffSize);
  if (_paramValue.str == NULL) {
      fprintf(stderr, "config file: %s, " \
          "malloc %d bytes fail\n", _lineInfo.filename, buffSize);
      free(szFormattedBase64);
      return ENOMEM;
  }

  /*
  printf("paramValue.str base64(%d): %.*s\n", nFormattedBase64,
      nFormattedBase64, szFormattedBase64);
  */

  bool decodeResult = ats_base64_decode(szFormattedBase64, nFormattedBase64,
      (unsigned char *)_paramValue.str, buffSize, (size_t *)&_paramValue.length);
  free(szFormattedBase64);
  if (!decodeResult) {
      fprintf(stderr, "config file: %s, " \
          "base64 decode fail, config line no: %d\n", _lineInfo.filename, _lineInfo.lineNo);
      return EINVAL;
  }

  /*
  printf("paramValue.str raw(%d): %.*s\n", _paramValue.length,
      _paramValue.length, _paramValue.str);
  */

  return 0;
}

const char *PluginParamParams::toString(char *buff, int *len)
{
  *len = sprintf(buff, "%s ", _directive->getName());
  if (_base64) {
    *len += sprintf(buff + *len, "%s ", BASE64_DIRECTIVE_STR);
  }

  if (_bBlock) {
    if (_base64) {
      *len += sprintf(buff + *len, "{ %.*s }", _base64Value.length, _base64Value.str);
    }
    else {
      *len += sprintf(buff + *len, "{ %.*s }", _paramValue.length, _paramValue.str);
    }
  }
  else {
    *len += sprintf(buff + *len, "%.*s", _paramValue.length, _paramValue.str);
  }

  return (const char *)buff;
}

