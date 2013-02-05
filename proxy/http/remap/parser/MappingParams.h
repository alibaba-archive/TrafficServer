#ifndef _MAPPING_PARAMS_H
#define _MAPPING_PARAMS_H

#include <string.h>
#include "DirectiveParams.h"

#define MAP_OPTION_WITH_RECV_PORT  "with_recv_port"
#define MAP_OPTION_REVERSE         "reverse"
#define REDIRECT_OPTION_TEMPORARY  "temporary"

class MappingParams : public DirectiveParams {
  public:
    MappingParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);

  public:
    virtual ~MappingParams() {}

    static inline const char *getOptionStr(const int flags) {
      switch(flags) {
        case MAP_FLAG_WITH_RECV_PORT:
          return MAP_OPTION_WITH_RECV_PORT;
        case MAP_FLAG_REVERSE:
          return MAP_OPTION_REVERSE;
        case REDIRECT_FALG_TEMPORARY:
          return REDIRECT_OPTION_TEMPORARY;
        default:
          return "";
      }
    }

    inline int getType() const {
      return _type;
    }

    inline int getFlags() const {
      return _flags;
    }

    inline const StringValue * getFromUrl() const {
      return _urls + 0;
    }

    inline const StringValue * getToUrl() const {
      return _urls + 1;
    }

  protected:
    const char *toString(char *buff, int *len);

    int _type;
    int _flags;
    StringValue _urls[2];  //0 for from url, 1 for to url
};


class MapParams : public MappingParams {
  public:
    MapParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);
    ~MapParams() {}
    int parse();
};


class RedirectParams: public MappingParams {
  public:
    RedirectParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);
    ~RedirectParams() {}
    int parse();
};

#endif

