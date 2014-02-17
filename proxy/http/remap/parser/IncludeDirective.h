#ifndef _INCLUDE_DIRECTIVE_H
#define _INCLUDE_DIRECTIVE_H

#include <string.h>
#include "RemapDirective.h"

class IncludeDirective : public RemapDirective {
  public:
    IncludeDirective();
    ~IncludeDirective();

    DirectiveParams *newDirectiveParams(const int rank, const char *filename,
        const int lineNo, const char *lineStr, const int lineLen,
        DirectiveParams *parent, const char *paramStr, const int paramLen,
        const bool bBlock);
};

#endif

