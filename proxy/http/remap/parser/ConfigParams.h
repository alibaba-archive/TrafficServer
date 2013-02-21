#ifndef _CONFIG_PARAMS_H
#define _CONFIG_PARAMS_H

#include <string.h>
#include "DirectiveParams.h"

#define CONFIG_TYPE_UNKOWN_STR        "UNKOWN"
#define CONFIG_TYPE_RECORDS_STR       "records"
#define CONFIG_TYPE_HOSTING_STR       "hosting"
#define CONFIG_TYPE_CACHE_CONTROL_STR "cache-control"
#define CONFIG_TYPE_CONGESTION_STR    "congestion"

class ConfigSetParams : public DirectiveParams {
  public:
    ConfigSetParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);

  public:
     ~ConfigSetParams() {}
     virtual int parse(const char *blockStat, const char *blockEnd);

     inline const ConfigKeyValue * getConfig() const {
       return &_config;
     }

  protected:
     int parseKV(StringValue *sv);
     const char *toString(char *buff, int *len);

     ConfigKeyValue _config;
};


class ConfigParams : public ConfigSetParams {
  public:
    ConfigParams(const int lineNo, const char *lineStr,
        const int lineLen, DirectiveParams *parent,
        RemapDirective *directive, const char *paramStr,
        const int paramLen, const bool bBlock);

  public:
     ~ConfigParams() {}

     int parse(const char *blockStat, const char *blockEnd);

     inline int getConfigType() const {
       return _config_type;
     }

     static inline int getConfigType(StringValue *sv) {
       if (sv->equals(CONFIG_TYPE_RECORDS_STR,
             sizeof(CONFIG_TYPE_RECORDS_STR) - 1))
       {
         return CONFIG_TYPE_RECORDS_INT;
       }
       if (sv->equals(CONFIG_TYPE_HOSTING_STR,
             sizeof(CONFIG_TYPE_HOSTING_STR) - 1))
       {
         return CONFIG_TYPE_HOSTING_INT;
       }
       if (sv->equals(CONFIG_TYPE_CACHE_CONTROL_STR,
             sizeof(CONFIG_TYPE_CACHE_CONTROL_STR) - 1))
       {
         return CONFIG_TYPE_CACHE_CONTROL_INT;
       }
       if (sv->equals(CONFIG_TYPE_CONGESTION_STR,
             sizeof(CONFIG_TYPE_CONGESTION_STR) - 1))
       {
         return CONFIG_TYPE_CONGESTION_INT;
       }

       return CONFIG_TYPE_NONE;
     }

     inline const char *getTypeCaption() {
       switch(_config_type) {
         case CONFIG_TYPE_RECORDS_INT:
           return CONFIG_TYPE_RECORDS_STR;
         case CONFIG_TYPE_HOSTING_INT:
           return CONFIG_TYPE_HOSTING_STR;
         case CONFIG_TYPE_CACHE_CONTROL_INT:
           return CONFIG_TYPE_CACHE_CONTROL_STR;
         case CONFIG_TYPE_CONGESTION_INT:
           return CONFIG_TYPE_CONGESTION_STR;
         default:
           return CONFIG_TYPE_UNKOWN_STR;
       }
     }

  protected:
     const char *toString(char *buff, int *len);
     int _config_type;
};

#endif

