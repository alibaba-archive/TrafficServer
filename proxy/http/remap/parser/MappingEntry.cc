#include "MappingParams.h"
#include "ConfigParams.h"
#include "MappingEntry.h"

void MappingEntry::print()
{
  char optionStr[256];
  printf("%03d. %s %s %s %s", _lineNo, (_type == MAPPING_TYPE_MAP) ?
      DIRECTVIE_NAME_MAP : DIRECTVIE_NAME_REDIRECT,
      MappingParams::getOptionStr(_flags & (~MAPPING_FLAG_HOST_REGEX), optionStr),
      _fromUrl.str, _toUrl.str);

  if (!this->hasChildren()) {
    printf("\n");
    return;
  }

  printf(" {\n");

  int i;
  for (i=0; i<_aclMethodIpCheckLists.count; i++) {
    _aclMethodIpCheckLists.items[i]->print();
  }
  for (i=0; i<_aclRefererCheckLists.count; i++) {
    _aclRefererCheckLists.items[i]->print();
  }

  for (i=0; i<_plugins.count; i++) {
    printf("\t%s %s", DIRECTVIE_NAME_PLUGIN, _plugins.items[i].filename.str);
    for (int k=0; k<_plugins.items[i].paramCount; k++) {
      printf(" %s", _plugins.items[i].params[k].str);
    }
    printf("\n");
  }

  for (i=0; i<CONFIG_TYPE_COUNT; i++) {
    DynamicArray<ConfigKeyValue> *configs = _configs + i;
    if (configs->count == 0) {
      continue;
    }

    const char *configTypeCaption;
    switch(i) {
      case CONFIG_TYPE_RECORDS_INDEX:
        configTypeCaption = CONFIG_TYPE_RECORDS_STR;
        break;
      case CONFIG_TYPE_HOSTING_INDEX:
        configTypeCaption = CONFIG_TYPE_HOSTING_STR;
        break;
      case CONFIG_TYPE_CACHE_INDEX:
        configTypeCaption = CONFIG_TYPE_CACHE_STR;
        break;
      case CONFIG_TYPE_CONGESTION_INDEX:
        configTypeCaption = CONFIG_TYPE_CONGESTION_STR;
        break;
      default:
        configTypeCaption = CONFIG_TYPE_UNKOWN_STR;
        break;
    }

    if (configs->count == 1) {
      printf("\t%s %s %s=%s\n", DIRECTVIE_NAME_CONFIG, configTypeCaption,
          configs->items[0].key.str, configs->items[0].value.str);
      continue;
    }

    printf("\t%s %s {\n", DIRECTVIE_NAME_CONFIG, configTypeCaption);
    for (int k=0; k<configs->count; k++) {
      printf("\t\t%s %s=%s\n", DIRECTVIE_NAME_CONFIG_SET,
          configs->items[k].key.str, configs->items[k].value.str);
    }
    printf("\t}\n");
  }

  printf("}\n");
}

int MappingEntry::addPlugin(const PluginInfo *pluginInfo)
{
      if (!_plugins.checkSize()) {
        return ENOMEM;
      }

      if (pluginInfo->duplicate(_plugins.items +
            _plugins.count) == NULL)
      {
        return ENOMEM;
      }
      _plugins.count++;

      return 0;
}

