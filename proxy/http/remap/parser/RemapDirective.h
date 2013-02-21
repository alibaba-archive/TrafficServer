
#ifndef  _REMAP_DIRECTIVE_H
#define  _REMAP_DIRECTIVE_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include "MappingTypes.h"
#include "RemapParser.h"
#include "DirectiveParams.h"

#define MAX_CHILD_NUM  8
#define MAX_DIRECTIVE_NAME_SIZE 32

#define DIRECTIVE_TYPE_NONE      0
#define DIRECTIVE_TYPE_STATEMENT 1
#define DIRECTIVE_TYPE_BLOCK     2
#define DIRECTIVE_TYPE_BOTH      3

#define DIRECTVIE_NAME_HTTP         "http"
#define DIRECTVIE_NAME_HTTPS        "https"
#define DIRECTVIE_NAME_TUNNEL       "tunnel"
#define DIRECTVIE_NAME_MAP          "map"
#define DIRECTVIE_NAME_REDIRECT     "redirect"
#define DIRECTVIE_NAME_PLUGIN       "plugin"
#define DIRECTVIE_NAME_PLUGIN_PARAM "param"
#define DIRECTVIE_NAME_ACL          "acl"
#define DIRECTVIE_NAME_CONFIG       "config"
#define DIRECTVIE_NAME_CONFIG_SET   "set"

#define DIRECTVIE_NAME_ACL_METHOD       "method"
#define DIRECTVIE_NAME_ACL_SRC_IP       "src_ip"
#define DIRECTVIE_NAME_ACL_REFERER      "referer"
#define DIRECTVIE_NAME_ACL_REDIRECT_URL "redirect_url"

class RemapParser;
class DirectiveParams;

class RemapDirective {
  friend class DirectiveParams;
  friend class RemapParser;

  protected:
    RemapDirective(const char *name, const int type,
        const int minParamCount, const int maxParamCount);

  public:
    virtual ~RemapDirective();

    inline const char *getName() const {
      return _name;
    }

    inline int getType() const {
      return _type;
    }

    inline int getChildrenCount() const {
      return _childrenCount;
    }

    inline int getMinParamCount() const {
      return _minParamCount;
    }

    inline int getMaxParamCount() const {
      return _maxParamCount;
    }

    virtual int check(DirectiveParams *params, const bool bBlock);

    virtual DirectiveParams *newDirectiveParams(const int lineNo,
        const char *lineStr, const int lineLen, DirectiveParams *parent,
        const char *paramStr, const int paramLen, const bool bBlock);

  protected:
    RemapDirective *getChild(const char *name);

    const char *_name;
    int _type;
    int _minParamCount;
    int _maxParamCount;
    int _childrenCount;
    RemapDirective *_forSearchChild;
    RemapDirective *_children[MAX_CHILD_NUM];  //sort by name
};

#endif

