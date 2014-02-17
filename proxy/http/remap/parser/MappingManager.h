#ifndef _MAPPING_MANAGER_H
#define _MAPPING_MANAGER_H

#include "MappingTypes.h"
#include "MappingEntry.h"
#include "MappingParams.h"
#include "PluginParams.h"
#include "ConfigParams.h"
#include "ACLMethodIpCheckList.h"
#include "ACLRefererCheckList.h"

struct ACLCheckListContainer {
  ACLMethodIpCheckList *methodIpCheckList;
  ACLRefererCheckList *refererCheckList;
};

class MappingManager {
  public:
    MappingManager() {}

    ~MappingManager() {
      for (int i=0; i<_mappings.count; i++) {
        delete _mappings.items[i];
      }
    }

    inline const DynamicArray<MappingEntry *> &getMappings() const {
      return _mappings;
    }

    int load(const DirectiveParams *rootParams);
    int expand();

    void print() {
      for (int i=0; i<_mappings.count; i++) {
        _mappings.items[i]->print();
      }
    }

    static bool isRegex(const char *str, const int length);

    static inline bool isRegex(const StringValue *sv) {
      return isRegex(sv->str, sv->length);
    }

    static bool findCharPair(const char *str, const int length,
        const char left, const char right,
        const char **start, const char **end);

    static inline bool findCharPair(const StringValue *sv,
        const char left, const char right,
        const char **start, const char **end)
    {
      return findCharPair(sv->str, sv->length,
          left, right, start, end);
    }

    static bool getRegexCaptures(const char *pattern, const int length,
        int *captures);

    static int replaceRegexReferenceIds(char *str,
        const int length, const int sub);

    static int getHostname(const StringValue *url,
        const char **start, const char **end);

    static int getPath(const StringValue *url, StringValue *path);

  protected:
    int loadMapping(const MappingParams *mappingParams,
        ACLCheckListContainer *parentCheckLists);

    int loadCheckLists(const DirectiveParams *rootParams,
        ACLCheckListContainer *checkLists);

    void addCheckLists(MappingEntry *mappingEntry,
        ACLCheckListContainer *checkLists);

    int loadMappingUrls(MappingEntry *mappingEntry,
        const MappingParams *mappingParams);

    bool getScheme(const StringValue *url, StringValue *scheme);

    int loadPlugins(MappingEntry *mappingEntry,
        const MappingParams *mappingParams);

    int loadConfigs(MappingEntry *mappingEntry,
        const MappingParams *mappingParams);

    int loadConfig(MappingEntry *mappingEntry,
        const ConfigParams *configParams);

    int addConfig(DynamicArray<ConfigKeyValue> *config,
        const ConfigKeyValue *configKV);

    bool isRegexSimpleRange(const StringValue *sv);

    void checkFullRegex(MappingEntry *mappingEntry);
    bool haveRegexReference(const char *str, const int length);
    bool isCrossReference(MappingEntry *mappingEntry);
    bool getRegexReferenceIds(const char *str, const int length,
        int *minId, int *maxId);

    DynamicArray<MappingEntry *> _mappings;
};

#endif

