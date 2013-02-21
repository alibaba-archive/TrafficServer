#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "DirectiveParams.h"


DirectiveParams::DirectiveParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock) : _parent(parent),
  _directive(directive), _bBlock(bBlock),
  _paramCount(0), _next(NULL)
{
  _lineInfo.lineNo = lineNo;
  _lineInfo.line.str = lineStr;
  _lineInfo.line.length = lineLen;

  _paramStr.str = paramStr;
  _paramStr.length = paramLen;
}


DirectiveParams::~DirectiveParams()
{
  //static int i =0;
  //fprintf(stderr, "destroy params: %p\n", this);

  if (_children.head != NULL) {
    DirectiveParams *child;
    DirectiveParams *tmp;

    child = _children.head;
    while (child != NULL) {
      tmp = child;
      child = child->_next;
      delete tmp;
      //fprintf(stderr, "destroy params %d\n", ++i);
    }

    _children.head = NULL;
    _children.tail = NULL;
  }
}

int DirectiveParams::init()
{
  const char *p;
  const char *pEnd;

  _paramCount = 0;
  pEnd = _paramStr.str + _paramStr.length;
  p = _paramStr.str;
  while (p < pEnd) {
    while (p < pEnd && (*p == ' ' || *p == '\t')) {
      p++;
    }

    if (p == pEnd) {
      break;
    }

    if (_paramCount >= MAX_PARAM_NUM) {
        fprintf(stderr, "file: "__FILE__", line: %d, "
            "too much parameters! config line: %.*s\n", __LINE__,
            _lineInfo.line.length, _lineInfo.line.str);
        return E2BIG;
    }

    if (*p == '"') {
      _params[_paramCount].str = ++p;  //skip quote
      while (p < pEnd && *p != '"') {
        p++;
      }

      if (p == pEnd) {
        fprintf(stderr, "file: "__FILE__", line: %d, "
            "unmatched quote! config line: %.*s\n", __LINE__,
            _lineInfo.line.length, _lineInfo.line.str);
        return EINVAL;
      }
      _params[_paramCount].length = p - _params[_paramCount].str;
      p++; //skip quote
    }
    else {
      _params[_paramCount].str = p;
      while (p < pEnd && !(*p == ' ' || *p == '\t')) {
        p++;
      }
      _params[_paramCount].length = p - _params[_paramCount].str;
    }

    _paramCount++;
  }

  return 0;
}

int DirectiveParams::getFirstParam(const char *paramStr, const int paramLen,
    StringValue *firstParam)
{
  const char *p;
  const char *pEnd;

  pEnd = paramStr + paramLen;
  p = paramStr;
  while (p < pEnd && (*p == ' ' || *p == '\t')) {
    p++;
  }
  if (p == pEnd) {
    firstParam->str = paramStr;
    firstParam->length = 0;
    return ENOENT;
  }

  firstParam->str = p;
  while (p < pEnd && !(*p == ' ' || *p == '\t')) {
    p++;
  }
  firstParam->length = p - firstParam->str;
  return 0;
}

int DirectiveParams::split(const StringValue *sv, const char seperator,
    StringValue *outputs, const int maxCount, int *count)
{
  const char *p;
  const char *pEnd;

  *count = 0;
  pEnd = sv->str + sv->length;
  p = sv->str;
  while (p < pEnd) {
    while (p < pEnd && (*p == seperator)) {
      p++;
    }

    if (p == pEnd) {
      break;
    }

    if (*count >= maxCount) {
        fprintf(stderr, "file: "__FILE__", line: %d, "
            "too much parameters! config line: %.*s\n", __LINE__,
            _lineInfo.line.length, _lineInfo.line.str);
        return ENOSPC;
    }

    if (*p == '"') {
      outputs[*count].str = ++p;  //skip quote
      while (p < pEnd && *p != '"') {
        p++;
      }

      if (p == pEnd) {
        fprintf(stderr, "file: "__FILE__", line: %d, "
            "unmatched quote! config line: %.*s\n", __LINE__,
            _lineInfo.line.length, _lineInfo.line.str);
        return EINVAL;
      }
      outputs[*count].length = p - outputs[*count].str;
      p++; //skip quote
    }
    else {
      outputs[*count].str = p;
      while (p < pEnd && !(*p == seperator)) {
        p++;
      }
      outputs[*count].length = p - outputs[*count].str;
    }

    (*count)++;
  }

  return 0;
}

const char *DirectiveParams::toString(char *buff, int *len)
{
  if (_directive == NULL) {
    *len = 0;
    return (const char *)buff;
  }

  *len = sprintf(buff, "%s", _directive->_name);
  for (int i=0; i<_paramCount; i++) {
    if (memchr(_params[i].str, ' ', _params[i].length) != NULL) {
      *len += sprintf(buff + *len, " \"%.*s\"", _params[i].length, _params[i].str);
    }
    else {
      *len += sprintf(buff + *len, " %.*s", _params[i].length, _params[i].str);
    }
  }

  return (const char *)buff;
}

void DirectiveParams::toString(StringBuffer *sb)
{
  int len;
  DirectiveParams *parent;

  if (sb->checkSize(2048) != 0) {
    return;
  }

  /*
  if (_parent != NULL) { //root is virtual, do NOT need output
    sb->length += sprintf(sb->str + sb->length, "%04d ", _lineInfo.lineNo);
  }
  */

  parent = _parent;
  while (parent != NULL) {
    *(sb->str + sb->length++) = '\t';
    parent = parent->_parent;
  }

  if (_parent != NULL) {  //root is virtual, do NOT need output
    toString(sb->str + sb->length, &len);
    sb->length += len;
  }

  if (_bBlock) {
    if (_parent != NULL) {
      *(sb->str + sb->length++) = ' ';
      *(sb->str + sb->length++) = '{';
      *(sb->str + sb->length++) = '\n';
    }

    DirectiveParams *child;
    child = _children.head;
    while (child != NULL) {
      child->toString(sb);
      child = child->_next;
    }
  }

  if (_parent == NULL) { //root is virtual, do NOT need output
    return;
  }

  if (sb->checkSize(32) != 0) {
    return;
  }

  if (_bBlock) {
    parent = _parent;
    while (parent != NULL) {
      *(sb->str + sb->length++) = '\t';
      parent = parent->_parent;
    }

    *(sb->str + sb->length++) = '}';
  }
  *(sb->str + sb->length++) = '\n';
}

