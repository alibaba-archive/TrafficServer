#ifndef _MAPPING_ENTRY_H
#define _MAPPING_ENTRY_H

#include "MappingTypes.h"
#include "ACLMethodIpCheckList.h"
#include "ACLRefererCheckList.h"

#define CONFIG_TYPE_RECORDS_INDEX       0
#define CONFIG_TYPE_HOSTING_INDEX       1
#define CONFIG_TYPE_CACHE_INDEX 2
#define CONFIG_TYPE_CONGESTION_INDEX    3
#define CONFIG_TYPE_COUNT               4


class MappingEntry {
  friend class MappingManager;

  public:
    MappingEntry(const int rank, const char *filename, const int lineNo,
        const int type, const int flags) :
      _needFree(true), _simpleRegexRange(false), _rank(rank),
      _lineNo(lineNo), _type(type), _flags(flags), _filename(filename)
    {
    }

    MappingEntry(const MappingEntry &src) : _needFree(false)
    {
      this->_rank = src._rank;
      this->_lineNo = src._lineNo;
      this->_type = src._type;
      this->_flags = src._flags;

      this->_fromUrl = src._fromUrl;
      this->_toUrl = src._toUrl;

      src._aclMethodIpCheckLists.duplicate(&this->_aclMethodIpCheckLists);
      src._aclRefererCheckLists.duplicate(&this->_aclRefererCheckLists);
      src._plugins.duplicate(&this->_plugins);

      for (int i=0; i<CONFIG_TYPE_COUNT; i++) {
        src._configs[i].duplicate(this->_configs + i);
      }
    }

    ~MappingEntry() {
      _fromUrl.free();
      _toUrl.free();

      if (_needFree) {
        int i;
        for (i=0; i<_plugins.count; i++) {
          _plugins.items[i].free();
        }

        for (i=0; i<CONFIG_TYPE_COUNT; i++) {
          DynamicArray<ConfigKeyValue> *configs = _configs + i;
          for (int k=0; k<configs->count; k++) {
            configs->items[k].free();
          }
        }
      }
    }

    inline const char *getFilename() const {
      return _filename;
    }

    inline int getRank() const {
      return _rank;
    }

    inline int getLineNo() const {
      return _lineNo;
    }

    inline int getType() const {
      return _type;
    }

    inline int getFlags() const {
      return _flags;
    }

    inline const StringValue *getFromUrl() const {
      return &_fromUrl;
    }

    inline const StringValue *getToUrl() const {
      return &_toUrl;
    }

    inline const DynamicArray<ACLMethodIpCheckList *> *getACLMethodIpCheckLists() const {
      return &_aclMethodIpCheckLists;
    }

    inline const DynamicArray<ACLRefererCheckList *> *getACLRefererCheckLists() const {
      return &_aclRefererCheckLists;
    }

    inline const DynamicArray<PluginInfo> *getPlugins() const {
      return &_plugins;
    }

    inline const DynamicArray<ConfigKeyValue> *getConfigs() const {
      return _configs;
    }

    bool hasChildren() const {
      if (_aclMethodIpCheckLists.count > 0 ||
          _aclRefererCheckLists.count > 0 || _plugins.count > 0)
      {
        return true;
      }

      for (int i=0; i<CONFIG_TYPE_COUNT; i++) {
        if (_configs[i].count > 0) {
          return true;
        }
      }

      return false;
    }

    const char *getRedirectUrl() const {
      const char *redirectUrl = NULL;
      const char *tmp;
      for (int k=0; k<_aclRefererCheckLists.count; k++) {
        tmp = _aclRefererCheckLists.items[k]->getRedirectUrl();
        if (*tmp != '\0') {
          redirectUrl = tmp;
        }
      }
      return redirectUrl;
    }

    int addPlugin(const PluginInfo *pluginInfo);

    void print();

  protected:
    bool _needFree;
    bool _simpleRegexRange;  //if include simple regex range such as: [0-9]
    int _rank;
    int _lineNo; //line no
    int _type;  //map or redirect
    int _flags; //regex, reverse etc
    const char *_filename;   //config filename

    StringValue _fromUrl;
    StringValue _toUrl;

    DynamicArray<ACLMethodIpCheckList *> _aclMethodIpCheckLists;
    DynamicArray<ACLRefererCheckList *> _aclRefererCheckLists;

    DynamicArray<PluginInfo> _plugins;  //plugin chain list
    DynamicArray<ConfigKeyValue> _configs[CONFIG_TYPE_COUNT];
};

#endif

