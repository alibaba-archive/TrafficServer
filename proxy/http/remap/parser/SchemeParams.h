#ifndef _SCHEME_PARAMS_H
#define _SCHEME_PARAMS_H

#include <string.h>
#include "MappingTypes.h"
#include "DirectiveParams.h"

class SchemeParams : public DirectiveParams {
  public:
    SchemeParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);
    ~SchemeParams() {}
    int parse();

    const char *getScheme() const {
      return _directive->getName();
    }

    const StringValue *getHost() const {
      return &_host;
    }

  protected:
     const char *toString(char *buff, int *len);
     StringValue _host;
};

#endif

