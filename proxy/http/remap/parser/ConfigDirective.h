#ifndef _CONFIG_DIRECTIVE_H
#define _CONFIG_DIRECTIVE_H

#include <string.h>
#include "RemapDirective.h"

class ConfigDirective : public RemapDirective {
  public:
    ConfigDirective();
    ~ConfigDirective();

    DirectiveParams *newDirectiveParams(const int rank, const char *filename,
        const int lineNo, const char *lineStr, const int lineLen,
        DirectiveParams *parent, const char *paramStr, const int paramLen,
        const bool bBlock);
};


class ConfigSetDirective : public RemapDirective {
  public:
    ConfigSetDirective();
    ~ConfigSetDirective();

    DirectiveParams *newDirectiveParams(const int rank, const char *filename,
        const int lineNo, const char *lineStr, const int lineLen,
        DirectiveParams *parent, const char *paramStr, const int paramLen,
        const bool bBlock);
};

#endif

