#ifndef _SCRIPT_DIRECTIVE_H
#define _SCRIPT_DIRECTIVE_H

#include <string.h>
#include "RemapDirective.h"

class ScriptDirective : public RemapDirective {
  public:
    ScriptDirective();
    ~ScriptDirective();

    DirectiveParams *newDirectiveParams(const int rank, const char *filename,
        const int lineNo, const char *lineStr, const int lineLen,
        DirectiveParams *parent, const char *paramStr, const int paramLen,
        const bool bBlock);
};

#endif

