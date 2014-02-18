
#ifndef  _DIRECTIVE_PARAMS_H
#define  _DIRECTIVE_PARAMS_H

#include "MappingTypes.h"
#include "RemapDirective.h"
#include "RemapParser.h"

struct StringBuffer {
  char *str;  //the string
  int length; //string length
  int size;   //alloc size

  StringBuffer() : str(NULL), length(0), size(0)
  {
  }

  StringBuffer(const int initSize) : str(NULL), length(0), size(0)
  {
    checkSize(initSize);
  }

  inline int checkSize(const int incSize)
  {
    if (size <= length + incSize) {
      size = (size + incSize) * 2;
      str = (char *)realloc(str, size);
      if (str == NULL) {
        fprintf(stderr, "file: "__FILE__", line: %d, "
            "malloc %d bytes fail, errnO; %d, error info: %s", __LINE__,
            size, errno, strerror(errno));
        size = 0;
        length = 0;
        return errno != 0 ? errno : ENOMEM;
      }
    }

    return 0;
  }
};

struct LineInfo {
  const char *filename;  //config filename
  int lineNo;
  StringValue line;
};

class RemapParser;
class RemapDirective;
class DirectiveParams;
class ACLActionParams;

struct ParamsEntryChain {
  DirectiveParams *head;
  DirectiveParams *tail;

  ParamsEntryChain() : head(NULL), tail(NULL)
  {
  }
};

class DirectiveParams {
  friend class RemapDirective;
  friend class RemapParser;
  friend class ACLActionParams;
  friend class PluginParams;

  public:
    DirectiveParams(const int rank, const char *filename, const int lineNo,
        const char *lineStr, const int lineLen,
        DirectiveParams *parent, RemapDirective *directive,
        const char *paramStr, const int paramLen, const bool bBlock);
    virtual ~DirectiveParams();

    int init();

    static int getFirstParam(const char *paramStr, const int paramLen,
        StringValue *firstParam);

    int setParams(const StringValue *params, const int paramCount) {
      if (paramCount < 0 || paramCount > MAX_PARAM_NUM) {
        fprintf(stderr, "file: "__FILE__", line: %d, "
            "too much parameters: %d! config line: %.*s\n", __LINE__,
            paramCount, _lineInfo.line.length, _lineInfo.line.str);
        return EINVAL;
      }

      _paramCount = paramCount;
      if (paramCount > 0) {
        memcpy(_params, params, sizeof(StringValue) * paramCount);
      }

      return 0;
    }

    inline void addChild(DirectiveParams *child) {
      if (_children.tail != NULL) {
        _children.tail->_next = child;
      }
      else {
        _children.head = child;
      }

      _children.tail = child;
    }

    void toString(StringBuffer *sb);

    virtual int parse(const char * /*blockStart*/, const char * /*blockEnd*/) {
      return 0;
    }

    inline int getRank() const {
      return _rank;
    }

    inline void setRank(const int rank) {
      _rank = rank;
    }

    inline void incRank(const int inc) {
      _rank += inc;
    }

    inline const LineInfo *getLineInfo() const {
      return &_lineInfo;
    }

    inline int getLineNo() const {
      return _lineInfo.lineNo;
    }

    inline const char *getFilename() const {
      return _lineInfo.filename;
    }

    inline void setLineNo(const int lineNo) {
      _lineInfo.lineNo = lineNo;
    }

    inline void setFilename(const char *filename) {
      _lineInfo.filename = filename;
    }

    inline int getParamCount() const {
      return _paramCount;
    }

    inline const StringValue *getParams() const {
      return _params;
    }

    inline const DirectiveParams *next() const {
      return _next;
    }

    inline const DirectiveParams *getChildren() const {
      return _children.head;
    }

    inline const DirectiveParams *getParent() const {
      return _parent;
    }

    inline bool isBlock() const {
      return _bBlock;
    }

  protected:
    int split(const StringValue *sv, const char seperator,
        StringValue *outputs, const int maxCount, int *count);
    virtual const char *toString(char *buff, int *len);

    DirectiveParams *_parent;
    RemapDirective *_directive;
    bool _bBlock; //block or single statement
    int _rank;
    LineInfo _lineInfo;
    StringValue _paramStr;

    int _paramCount;
    StringValue _params[MAX_PARAM_NUM];
    DirectiveParams *_next;  //for children chain
    ParamsEntryChain _children;
};

#endif

