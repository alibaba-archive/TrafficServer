#include "ACLParams.h"
#include "ACLDirective.h"

static StringIntPair allMethods[ACL_METHOD_MAX_NUM] = {
  {StringValue("GET", sizeof("GET") - 1), ACL_METHOD_FLAG_GET},
  {StringValue("POST", sizeof("POST") - 1), ACL_METHOD_FLAG_POST},
  {StringValue("PURGE", sizeof("PURGE") - 1), ACL_METHOD_FLAG_PURGE},
  {StringValue("HEAD", sizeof("HEAD") - 1), ACL_METHOD_FLAG_HEAD},
  {StringValue("PUT", sizeof("PUT") - 1), ACL_METHOD_FLAG_PUT},
  {StringValue("DELETE", sizeof("DELETE") - 1), ACL_METHOD_FLAG_DELETE},
  {StringValue("CONNECT", sizeof("CONNECT") - 1), ACL_METHOD_FLAG_CONNECT},
  {StringValue("ICP_QUERY", sizeof("ICP_QUERY") - 1), ACL_METHOD_FLAG_ICP_QUERY},
  {StringValue("OPTIONS", sizeof("OPTIONS") - 1), ACL_METHOD_FLAG_OPTIONS},
  {StringValue("TRACE", sizeof("TRACE") - 1), ACL_METHOD_FLAG_TRACE},
  {StringValue("PUSH", sizeof("PUSH") - 1), ACL_METHOD_FLAG_PUSH},
  {StringValue("all", sizeof("all") - 1), ACL_METHOD_FLAG_ALL}
};

ACLMethodParams::ACLMethodParams(const int rank, const char *filename, const int lineNo,
    const char *lineStr, const int lineLen, DirectiveParams *parent,
    RemapDirective *directive, const char *paramStr,
    const int paramLen, const bool bBlock) :
  DirectiveParams(rank, filename, lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock)
{
}

int ACLMethodParams::getMethodFlag(const StringValue *sv)
{
  for (int i=0; i<ACL_METHOD_MAX_NUM; i++) {
    if (allMethods[i].s.equals(sv)) {
      return allMethods[i].i;
    }
  }

  return ACL_METHOD_FLAG_NONE;
}

int ACLMethodParams::parse(const char *blockStart, const char *blockEnd)
{
  int methodCount;
  StringValue methods[ACL_METHOD_MAX_NUM];

  if (_paramCount != 1) {
    fprintf(stderr, "config file: %s, " \
        "invalid parameter count: %d! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _paramCount, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  int result = split(&_params[0], ACL_SEPERATOR_CHAR,
      methods, ACL_METHOD_MAX_NUM, &methodCount);
  if (result != 0) {
    return result;
  }

  if (methodCount == 0) {
    fprintf(stderr, "config file: %s, " \
        "expect method! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return ENOENT;
  }

  int flag;
  _methodFlags = 0;
  for (int i=0; i<methodCount; i++) {
    flag = getMethodFlag(&methods[i]);
    if (flag == ACL_METHOD_FLAG_NONE) {
      fprintf(stderr, "config file: %s, " \
          "invalid method: %.*s! config line no: %d, line: %.*s\n",
          _lineInfo.filename, methods[i].length, methods[i].str, _lineInfo.lineNo,
          _lineInfo.line.length, _lineInfo.line.str);
      return EINVAL;
    }

    _methodFlags |= flag;
  }

  return 0;
}

const char *ACLMethodParams::getMethodString(const int methodFlags,
    char *buff, int *len)
{
  if (methodFlags == ACL_METHOD_FLAG_ALL) {
    *len = sprintf(buff, "%s", ACL_STR_ALL);
    return buff;
  }

  *len = 0;
  for (int i=0; i<ACL_METHOD_MAX_NUM; i++) {
    if ((methodFlags & allMethods[i].i) == allMethods[i].i) {
      if (*len > 0) {
        *(buff + (*len)++) = ACL_SEPERATOR_CHAR;
      }

      memcpy(buff + *len, allMethods[i].s.str, allMethods[i].s.length);
      *len += allMethods[i].s.length;
    }
  }

  *(buff + *len) = '\0';
  return buff;
}

const char *ACLMethodParams::toString(char *buff, int *len)
{
  int newLen;
  *len = sprintf(buff, "%s ", _directive->getName());

  this->getMethodString(_methodFlags, buff + *len, &newLen);
  *len += newLen;
  return (const char *)buff;
}


ACLSrcIpParams::ACLSrcIpParams(const int rank, const char *filename, const int lineNo,
    const char *lineStr, const int lineLen, DirectiveParams *parent,
    RemapDirective *directive, const char *paramStr,
    const int paramLen, const bool bBlock) :
  DirectiveParams(rank, filename, lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock)
{
}

int ACLSrcIpParams::parse(const char *blockStart, const char *blockEnd)
{
  if (_paramCount != 1) {
    fprintf(stderr, "config file: %s, " \
        "invalid parameter count: %d! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _paramCount, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  _ip = _params[0];
  return 0;
}

const char *ACLSrcIpParams::toString(char *buff, int *len)
{
  *len = sprintf(buff, "%s %.*s", _directive->getName(), _ip.length, _ip.str);
  return (const char *)buff;
}


ACLRedirectUrlParams::ACLRedirectUrlParams(const int rank, const char *filename,
    const int lineNo, const char *lineStr, const int lineLen,
    DirectiveParams *parent, RemapDirective *directive, const char *paramStr,
    const int paramLen, const bool bBlock, const bool primaryDirective) :
  DirectiveParams(rank, filename, lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock)
{
  _primaryDirective = primaryDirective;
}

int ACLRedirectUrlParams::parse(const char *blockStart, const char *blockEnd)
{
  if (_parent->getParent() == NULL) {
    fprintf(stderr, "config file: %s, " \
        "acl redirect_url can't in top level! " \
        "config line no: %d, line: %.*s\n", _lineInfo.filename,
        _lineInfo.lineNo, _lineInfo.line.length, _lineInfo.line.str);
    return EINVAL;
  }

  int startIndex = _primaryDirective ? 0 : 1;
  if (_paramCount != startIndex + 1) {
    fprintf(stderr, "config file: %s, " \
        "invalid parameter count: %d! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _paramCount, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  _url = _params[startIndex];
  return 0;
}

const char *ACLRedirectUrlParams::toString(char *buff, int *len)
{
  if (_primaryDirective) {
    *len = sprintf(buff, "%s %.*s", _directive->getName(),
        _url.length, _url.str);
  }
  else {
    *len = sprintf(buff, "%s %.*s %.*s", _directive->getName(),
        _params[0].length, _params[0].str, _url.length, _url.str);
  }

  return (const char *)buff;
}

ACLRefererParams::ACLRefererParams(const int rank, const char *filename, const int lineNo,
    const char *lineStr, const int lineLen, DirectiveParams *parent,
    RemapDirective *directive, const char *paramStr,
    const int paramLen, const bool bBlock) :
  DirectiveParams(rank, filename, lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock)
{
}

int ACLRefererParams::parse(const char *blockStart, const char *blockEnd)
{
  if (_paramCount == 0) {
    fprintf(stderr, "config file: %s, " \
        "invalid parameter count: %d! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _paramCount, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  StringValue *sv = &_params[0];
  if (_paramCount == 1) {
    if (sv->equals(ACL_REFERER_TYPE_EMPTY_STR,
          sizeof(ACL_REFERER_TYPE_EMPTY_STR) - 1))
    {
      _refererType = ACL_REFERER_TYPE_EMPTY_INT;
      _partCount = 0;
    }
    else if (sv->equals(ACL_STR_ALL, sizeof(ACL_STR_ALL) - 1)) {
      _refererType = ACL_REFERER_TYPE_ALL_INT;
      _partCount = 0;
    }
    else {
      fprintf(stderr, "config file: %s, " \
          "invalid referer sub keywords: %.*s! " \
          "config line no: %d, line: %.*s\n",
          _lineInfo.filename, sv->length, sv->str, _lineInfo.lineNo,
          _lineInfo.line.length, _lineInfo.line.str);
      return EINVAL;
    }
  }
  else {
    if (_paramCount != 2) {
      fprintf(stderr, "config file: %s, " \
          "invalid parameter count: %d! config line no: %d, line: %.*s\n",
          _lineInfo.filename, _paramCount, _lineInfo.lineNo, _lineInfo.line.length,
          _lineInfo.line.str);
      return EINVAL;
    }

    if (sv->equals(ACL_REFERER_TYPE_REGEX_STR,
          sizeof(ACL_REFERER_TYPE_REGEX_STR) - 1))
    {
      _refererType = ACL_REFERER_TYPE_REGEX_INT;
      _partCount = 1;
      _parts[0] = _params[1];
    }
    else if (sv->equals(ACL_REFERER_TYPE_HOST_STR,
          sizeof(ACL_REFERER_TYPE_HOST_STR) - 1) ||
        sv->equals(ACL_REFERER_TYPE_DOMAIN_STR,
          sizeof(ACL_REFERER_TYPE_DOMAIN_STR) - 1))
    {
      int result = split(&_params[1], ACL_SEPERATOR_CHAR,
          _parts, ACL_REFERER_MAX_SPLIT_PARTS, &_partCount);
      if (result != 0) {
        return result;
      }

      if (_partCount == 0) {
        fprintf(stderr, "config file: %s, " \
            "expect referer parameter! config line no: %d, line: %.*s\n",
            _lineInfo.filename, _lineInfo.lineNo, _lineInfo.line.length,
            _lineInfo.line.str);
        return ENOENT;
      }

      if (sv->equals(ACL_REFERER_TYPE_HOST_STR,
            sizeof(ACL_REFERER_TYPE_HOST_STR) - 1))
      {
        _refererType = ACL_REFERER_TYPE_HOST_INT;
      }
      else {
        _refererType = ACL_REFERER_TYPE_DOMAIN_INT;
      }
    }
    else {
      fprintf(stderr, "config file: %s, " \
          "invalid referer parameter: %.*s! config line no: %d, line: %.*s\n",
          _lineInfo.filename, sv->length, sv->str, _lineInfo.lineNo,
          _lineInfo.line.length, _lineInfo.line.str);
      return EINVAL;
    }
  }

  return 0;
}

const char *ACLRefererParams::toString(char *buff, int *len)
{
  *len = sprintf(buff, "%s %s", _directive->getName(),
      this->getRefererTypeString());
  if (_refererType == ACL_REFERER_TYPE_EMPTY_INT ||
      _refererType == ACL_REFERER_TYPE_ALL_INT)
  {
  }
  else if (_refererType == ACL_REFERER_TYPE_REGEX_INT) {
    *len += sprintf(buff + *len, " %.*s", _parts[0].length, _parts[0].str);
  }
  else {
    *(buff + (*len)++) = ' ';
    for (int i=0; i<_partCount; i++) {
      if (i > 0) {
        *(buff + (*len)++) = ACL_SEPERATOR_CHAR;
      }

      memcpy(buff + *len, _parts[i].str, _parts[i].length);
      *len += _parts[i].length;
    }

    *(buff + *len) = '\0';
  }

  return (const char *)buff;
}


ACLDefineParams::ACLDefineParams(const int rank, const char *filename, const int lineNo,
    const char *lineStr, const int lineLen, DirectiveParams *parent,
    RemapDirective *directive, const char *paramStr,
    const int paramLen, const bool bBlock) :
  DirectiveParams(rank, filename, lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock)
{
}

int ACLDefineParams::parse(const char *blockStart, const char *blockEnd)
{
  if (_parent->getParent() != NULL) {
    fprintf(stderr, "config file: %s, " \
        "acl define must in top level! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  if (!_bBlock) {
    fprintf(stderr, "config file: %s, " \
        "invalid config format! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  if (_paramCount != 3) {
    fprintf(stderr, "config file: %s, " \
        "invalid parameter count: %d! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _paramCount, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  _aclName = _params[1];
  StringValue *sv = &_params[2];
  if (sv->equals(ACL_ACTION_ALLOW_STR, sizeof(ACL_ACTION_ALLOW_STR) - 1)) {
    _action = ACL_ACTION_ALLOW_INT;
  }
  else if (sv->equals(ACL_ACTION_DENY_STR, sizeof(ACL_ACTION_DENY_STR) - 1)) {
    _action = ACL_ACTION_DENY_INT;
  }
  else {
    fprintf(stderr, "config file: %s, " \
        "invalid config format, unkown keywords: \"%.*s\"! " \
        "config line no: %d, line: %.*s\n",
        _lineInfo.filename, sv->length, sv->str, _lineInfo.lineNo,
        _lineInfo.line.length, _lineInfo.line.str);
    return EINVAL;
  }

  return 0;
}

const char *ACLDefineParams::toString(char *buff, int *len)
{
  *len = sprintf(buff, "%s %s %.*s %s", _directive->getName(),
      ACL_SECOND_DIRECTIVE_DEFINE_STR, _aclName.length, _aclName.str,
      this->getActionString());

  return (const char *)buff;
}


ACLCheckParams::ACLCheckParams(const int rank, const char *filename, const int lineNo,
    const char *lineStr, const int lineLen, DirectiveParams *parent,
    RemapDirective *directive, const char *paramStr,
    const int paramLen, const bool bBlock) :
  DirectiveParams(rank, filename, lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock)
{
}

int ACLCheckParams::parse(const char *blockStart, const char *blockEnd)
{
  if (_parent->getParent() == NULL) {
    fprintf(stderr, "config file: %s, " \
        "acl check can't in top level! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  if (_bBlock) {
    fprintf(stderr, "config file: %s, " \
        "invalid config format! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  if (_paramCount != 2) {
    fprintf(stderr, "config file: %s, " \
        "invalid acl parameter count: %d! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _paramCount, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  _aclName = _params[1];
  return 0;
}


const char *ACLCheckParams::toString(char *buff, int *len)
{
  *len = sprintf(buff, "%s %s %.*s", _directive->getName(),
      ACL_SECOND_DIRECTIVE_CHECK_STR, _aclName.length, _aclName.str);

  return (const char *)buff;
}


ACLActionParams::ACLActionParams(const int rank, const char *filename, const int lineNo,
    const char *lineStr, const int lineLen, DirectiveParams *parent,
    RemapDirective *directive, const char *paramStr,
    const int paramLen, const bool bBlock) :
  DirectiveParams(rank, filename, lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock), _action(0), _actionParams(NULL)
{
}

int ACLActionParams::parse(const char *blockStart, const char *blockEnd)
{
  if (_parent->getParent() == NULL) {
    fprintf(stderr, "config file: %s, " \
        "acl %s can't in top level! config line no: %d, line: %.*s\n",
        _lineInfo.filename, this->getActionString(), _lineInfo.lineNo,
        _lineInfo.line.length, _lineInfo.line.str);
    return EINVAL;
  }

  if (_bBlock) {
    fprintf(stderr, "config file: %s, " \
        "invalid config format! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  if (_paramCount == 2) {  //such as: allow all, deny all
    StringValue *sv = &_params[1];
    if (sv->equals(ACL_STR_ALL, sizeof(ACL_STR_ALL) - 1)) {
      _actionParams = NULL;
    }
    else {
      fprintf(stderr, "config file: %s, " \
          "invalid config format, unkown keywords: \"%.*s\"! " \
          "config line no: %d, line: %.*s\n",
          _lineInfo.filename, sv->length, sv->str, _lineInfo.lineNo,
          _lineInfo.line.length, _lineInfo.line.str);
      return EINVAL;
    }
  }
  else if (_paramCount >= 3) {  //such as: allow method GET
    const char *paramStr;
    if (*(_params[2].str - 1) == '"') {
      paramStr = _params[2].str - 1;
    }
    else {
      paramStr = _params[2].str;
    }
    int paramLen = (_paramStr.str + _paramStr.length) - paramStr;

    StringValue *sv = &_params[1];
    if (sv->equals(DIRECTVIE_NAME_ACL_METHOD,
          sizeof(DIRECTVIE_NAME_ACL_METHOD) - 1))
    {
      _actionParams = new ACLMethodParams(_rank, _lineInfo.filename,
          _lineInfo.lineNo, _lineInfo.line.str, _lineInfo.line.length,
          _parent, ((ACLDirective *)_directive)->getMethodDirective(),
          paramStr, paramLen, _bBlock);
    }
    else if (sv->equals(DIRECTVIE_NAME_ACL_SRC_IP,
          sizeof(DIRECTVIE_NAME_ACL_SRC_IP) - 1))
    {
      _actionParams = new ACLSrcIpParams(_rank, _lineInfo.filename,
          _lineInfo.lineNo, _lineInfo.line.str, _lineInfo.line.length,
          _parent, ((ACLDirective *)_directive)->getSrcIpDirective(),
          paramStr, paramLen, _bBlock);
    }
    else if (sv->equals(DIRECTVIE_NAME_ACL_REFERER,
          sizeof(DIRECTVIE_NAME_ACL_REFERER) - 1))
    {
      _actionParams = new ACLRefererParams(_rank, _lineInfo.filename,
          _lineInfo.lineNo, _lineInfo.line.str, _lineInfo.line.length,
          _parent, ((ACLDirective *)_directive)->getRefererDirective(),
          paramStr, paramLen, _bBlock);
    }
    else {
      fprintf(stderr, "config file: %s, " \
          "invalid config format, unkown keywords: \"%.*s\"! " \
          "config line no: %d, line: %.*s\n",
          _lineInfo.filename, sv->length, sv->str, _lineInfo.lineNo,
          _lineInfo.line.length, _lineInfo.line.str);
      return EINVAL;
    }

    _actionParams->setParams(_params + 2, _paramCount - 2);
    return _actionParams->parse(blockStart, blockEnd);
  }
  else {
    fprintf(stderr, "config file: %s, " \
        "invalid acl parameter count: %d! config line no: %d, line: %.*s\n",
        _lineInfo.filename, _paramCount, _lineInfo.lineNo, _lineInfo.line.length,
        _lineInfo.line.str);
    return EINVAL;
  }

  return 0;
}

const char *ACLActionParams::toString(char *buff, int *len)
{
  *len = sprintf(buff, "%s %s ", _directive->getName(),
      this->getActionString());
  if (_actionParams == NULL) {
    *len += sprintf(buff + *len, "%s", ACL_STR_ALL);
  }
  else {
    int newLen;
    _actionParams->toString(buff + *len, &newLen);
    *len += newLen;
  }

  return (const char *)buff;
}


ACLAllowParams::ACLAllowParams(const int rank, const char *filename, const int lineNo,
    const char *lineStr, const int lineLen, DirectiveParams *parent,
    RemapDirective *directive, const char *paramStr,
    const int paramLen, const bool bBlock) :
  ACLActionParams(rank, filename, lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock)
{
  _action = ACL_ACTION_ALLOW_INT;
}


ACLDenyParams::ACLDenyParams(const int rank, const char *filename, const int lineNo,
    const char *lineStr, const int lineLen, DirectiveParams *parent,
    RemapDirective *directive, const char *paramStr,
    const int paramLen, const bool bBlock) :
  ACLActionParams(rank, filename, lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock)
{
  _action = ACL_ACTION_DENY_INT;
}
 
