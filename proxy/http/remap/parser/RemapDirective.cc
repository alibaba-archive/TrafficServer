#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "RemapDirective.h"

RemapDirective::RemapDirective(const char *name, const int type,
    const int minParamCount, const int maxParamCount) : _name(name),
  _type(type), _minParamCount(minParamCount), _maxParamCount(maxParamCount),
  _childrenCount(0), _forSearchChild(NULL)
{
}

RemapDirective::~RemapDirective()
{
  if (_forSearchChild != NULL) {
    delete _forSearchChild;
    _forSearchChild = NULL;
  }

  //fprintf(stderr, "destroy directive: %s\n", _name);

  if (_childrenCount > 0) {
    for (int i=0; i<_childrenCount; i++) {
      if (_children[i] != NULL) {
        delete _children[i];
        _children[i] = NULL;
      }
    }

    _childrenCount = 0;
  }
}

DirectiveParams *RemapDirective::newDirectiveParams(const int lineNo,
    const char *lineStr, const int lineLen, DirectiveParams *parent,
    const char *paramStr, const int paramLen, const bool bBlock)
{
  return new DirectiveParams(lineNo, lineStr, lineLen, parent,
      this, paramStr, paramLen, bBlock);
}

static int compare(const void *p1, const void *p2)
{
  return strcmp((*((RemapDirective **)p1))->getName(),
      (*((RemapDirective **)p2))->getName());
}

RemapDirective *RemapDirective::getChild(const char *name)
{
  if (_forSearchChild == NULL) {
    _forSearchChild = new RemapDirective(name, DIRECTIVE_TYPE_NONE, 0, 0);
    qsort(_children, _childrenCount, sizeof(RemapDirective *), compare);
  }
  else {
    _forSearchChild->_name = name;
  }

  RemapDirective **found;
  found = (RemapDirective **)bsearch(&_forSearchChild, _children,
      _childrenCount, sizeof(RemapDirective *), compare);
  if (found != NULL) {
    return *found;
  }
  else {
    return NULL;
  }
}

int RemapDirective::check(DirectiveParams *params, const bool bBlock)
{
  int result;

  if (_type == DIRECTIVE_TYPE_BLOCK && !bBlock) {
    fprintf(stderr, "file: "__FILE__", line: %d, "
        "expect block { and }! config line: %.*s\n", __LINE__,
        params->_lineInfo.line.length, params->_lineInfo.line.str);
    return EINVAL;
  }

  if (_type == DIRECTIVE_TYPE_STATEMENT && bBlock) {
    fprintf(stderr, "file: "__FILE__", line: %d, "
        "unexpect block { and }! config line: %.*s\n", __LINE__,
        params->_lineInfo.line.length, params->_lineInfo.line.str);
    return EINVAL;
  }

  if ((result=params->init()) != 0) {
    return result;
  }

  if (params->_paramCount < _minParamCount) {
    fprintf(stderr, "file: "__FILE__", line: %d, "
        "parameter count: %d < %d, config line: %.*s\n", __LINE__,
        params->_paramCount, _minParamCount, params->_lineInfo.line.length,
        params->_lineInfo.line.str);
    return EINVAL;
  }

  if (_maxParamCount > 0 && params->_paramCount > _maxParamCount) {
    fprintf(stderr, "file: "__FILE__", line: %d, "
        "parameter count: %d > %d, config line: %.*s\n", __LINE__,
        params->_paramCount, _maxParamCount, params->_lineInfo.line.length,
        params->_lineInfo.line.str);
    return EINVAL;
  }

  return 0;
}

