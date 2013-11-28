#ifndef _MAPPING_PARAMS_H
#define _MAPPING_PARAMS_H

#include <string.h>
#include "DirectiveParams.h"

#define MAP_OPTION_WITH_RECV_PORT  "with_recv_port"
#define MAP_OPTION_REVERSE         "reverse"
#define REDIRECT_OPTION_TEMPORARY  "temporary"
#define MAPPING_OPTION_PATH_REGEX  "regex"

class MappingParams : public DirectiveParams {
  public:
    MappingParams(const int rank, const char *filename, const int lineNo,
        const char *lineStr, const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);

  public:
    virtual ~MappingParams() {}

    static inline const char *getOptionStr(const int flags, char *optionStr) {
      switch(flags & (~MAPPING_FLAG_PATH_REGEX)) {
        case MAP_FLAG_WITH_RECV_PORT:
          strcpy(optionStr, MAP_OPTION_WITH_RECV_PORT);
          break;
        case MAP_FLAG_REVERSE:
          strcpy(optionStr, MAP_OPTION_REVERSE);
          break;
        case REDIRECT_FALG_TEMPORARY:
          strcpy(optionStr, REDIRECT_OPTION_TEMPORARY);
          break;
        default:
          strcpy(optionStr, "");
          break;
      }
      if (flags & MAPPING_FLAG_PATH_REGEX) {
        sprintf(optionStr + strlen(optionStr), " %s", MAPPING_OPTION_PATH_REGEX);
      }

      return optionStr;
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
    MapParams(const int rank, const char *filename, const int lineNo,
        const char *lineStr, const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);
    ~MapParams() {}
    int parse(const char *blockStart, const char *blockEnd);
};


class RedirectParams: public MappingParams {
  public:
    RedirectParams(const int rank, const char *filename, const int lineNo,
        const char *lineStr, const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);
    ~RedirectParams() {}
    int parse(const char *blockStart, const char *blockEnd);
};

#endif

