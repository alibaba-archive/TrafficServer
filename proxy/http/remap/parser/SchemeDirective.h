#ifndef _SCHEME_DIRECTIVE_H
#define _SCHEME_DIRECTIVE_H

#include <string.h>
#include "RemapDirective.h"

class SchemeDirective : public RemapDirective {
  public:
    SchemeDirective(const char *name);
    ~SchemeDirective();

    DirectiveParams *newDirectiveParams(const int lineNo, 
        const char *lineStr, const int lineLen, DirectiveParams *parent, 
        const char *paramStr, const int paramLen, const bool bBlock);
};

#endif

