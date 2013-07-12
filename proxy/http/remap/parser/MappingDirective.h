#ifndef _MAPPING_DIRECTIVE_H
#define _MAPPING_DIRECTIVE_H

#include <string.h>
#include "RemapDirective.h"

class MappingDirective : public RemapDirective {
  protected:
    MappingDirective(const char *name);

  public:
    virtual ~MappingDirective() {}
};


class MapDirective : public MappingDirective {
  public:
    MapDirective();
    MapDirective(const char *name);
    ~MapDirective() {}

    DirectiveParams *newDirectiveParams(const int lineNo,
        const char *lineStr, const int lineLen, DirectiveParams *parent,
        const char *paramStr, const int paramLen, const bool bBlock);
};


class RedirectDirective : public MappingDirective {
  public:
    RedirectDirective();
    ~RedirectDirective() {}

    DirectiveParams *newDirectiveParams(const int lineNo,
        const char *lineStr, const int lineLen, DirectiveParams *parent,
        const char *paramStr, const int paramLen, const bool bBlock);
};

//for backward compatibility
class RegexMapDirective : public MapDirective {
  public:
    RegexMapDirective();
    ~RegexMapDirective() {}
};

#endif

