#ifndef _INCLUDE_PARAMS_H
#define _INCLUDE_PARAMS_H

#include <string.h>
#include "MappingTypes.h"
#include "RemapDirective.h"
#include "DirectiveParams.h"

class IncludeParams : public DirectiveParams {
  public:
    IncludeParams(const int rank, const char *filename, const int lineNo,
        const char *lineStr, const int lineLen,
        DirectiveParams *parent, RemapDirective *directive,
        const char *paramStr, const int paramLen, const bool bBlock);
    ~IncludeParams();
    int parse(const char *blockStart, const char *blockEnd);

    const DynamicArray<char *> *getFilenames() const {
      return &_filenames;
    }

  protected:
    int expandFilenames();
    int listFile(const char *filepath, const char *filename);

    const char *toString(char *buff, int *len);
    char _rawFilename[1024];
    DynamicArray<char *> _filenames;  //expand filenames
};

#endif

