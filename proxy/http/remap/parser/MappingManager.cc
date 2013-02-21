#include <stdio.h>
#include "SchemeParams.h"
#include "MappingManager.h"

int MappingManager::loadCheckLists(const DirectiveParams *rootParams,
        ACLCheckListContainer *checkLists)
{
  int result;
  checkLists->methodIpCheckList = new ACLMethodIpCheckList;
  if (checkLists->methodIpCheckList == NULL) {
    return ENOMEM;
  }

  result = checkLists->methodIpCheckList->load(rootParams);
  if (result != 0 || checkLists->methodIpCheckList->empty()) {
    delete checkLists->methodIpCheckList;
    checkLists->methodIpCheckList = NULL;

    if (result != 0) {
      return result;
    }
  }

  do {
    checkLists->refererCheckList = new ACLRefererCheckList;
    if (checkLists->refererCheckList == NULL) {
      result = ENOMEM;
      break;
    }

    result = checkLists->refererCheckList->load(rootParams);
    if (result != 0 || checkLists->refererCheckList->empty()) {
      delete checkLists->refererCheckList;
      checkLists->refererCheckList = NULL;
    }
  }
  while (0);

  if (result != 0 && checkLists->methodIpCheckList != NULL) {
    delete checkLists->methodIpCheckList;
    checkLists->methodIpCheckList = NULL;
  }

  return result;
}

int MappingManager::load(const DirectiveParams *rootParams)
{
  int result;
  const SchemeParams *schemeParams;
  const MappingParams *mappingParams;
  const DirectiveParams *children = rootParams->getChildren();
  while (children != NULL) {
    if ((schemeParams=dynamic_cast<const SchemeParams *>(
            children)) != NULL)
    {
      ACLCheckListContainer checkLists;
      if ((result=this->loadCheckLists(schemeParams, &checkLists)) != 0) {
        return result;
      }

      const DirectiveParams *subChild = schemeParams->getChildren();
      while (subChild != NULL) {
       if ((mappingParams =dynamic_cast<const MappingParams *>(
            subChild)) != NULL)
       {
         if ((result=this->loadMapping(mappingParams, &checkLists)) != 0) {
           return result;
         }
       }

        subChild = subChild->next();
      }
    }
    else if ((mappingParams =dynamic_cast<const MappingParams *>(
            children)) != NULL)
    {
         if ((result=this->loadMapping(mappingParams, NULL)) != 0) {
           return result;
         }
    }

    children = children->next();
  }

  return 0;
}

bool MappingManager::getScheme(const StringValue *url, StringValue *scheme)
{
  const char *colon = (const char *)memchr(url->str, ':', url->length);
  if (colon == NULL) {
    scheme->str = NULL;
    scheme->length = 0;
    return false;
  }

  if ((url->str + url->length) - colon >= 3 &&
    *(colon + 1) == '/' && *(colon + 2) == '/')
  {
    scheme->str = url->str;
    scheme->length = colon - url->str;

    return true;
  }
  else {
    scheme->str = NULL;
    scheme->length = 0;
    return false;
  }
}

bool MappingManager::findCharPair(const char *str, const int length,
    const char left, const char right,
    const char **start, const char **end)
{
  *start = (const char *)memchr(str, left, length);
  if (*start == NULL) {
    *end = NULL;
    return false;
  }

  if ((*end=(const char *)memchr(*start + 1, right, (str +
            length) - (*start + 1))) != NULL)
  {
    (*end)++;
    return true;
  }
  else {
    return false;
  }
}

bool MappingManager::isRegex(const char *str, const int length)
{
  const char *start;
  const char *end;

  if (length == 0) {
    return false;
  }

  if (*str == '^' || memchr(str, '$', length) != NULL) {
    return true;
  }

  if (memchr(str, '*', length) != NULL) {
    return true;
  }

  if (memchr(str, '+', length) != NULL) {
    return true;
  }

  if (memchr(str, '|', length) != NULL) {
    return true;
  }

  if (memchr(str, '?', length) != NULL) {
    return true;
  }

  if (memchr(str, '\\', length) != NULL) {
    return true;
  }

  if (findCharPair(str, length, '(', ')', &start, &end)) {
    return true;
  }

  if (findCharPair(str, length, '[', ']', &start, &end)) {
    return true;
  }

  if (findCharPair(str, length, '{', '}', &start, &end)) {
    return true;
  }

  return false;
}

bool MappingManager::isRegexSimpleRange(const StringValue *sv)
{
  const char *start;
  const char *end;

  if (memchr(sv->str, '*', sv->length) != NULL) {
    return false;
  }

  if (memchr(sv->str, '+', sv->length) != NULL) {
    return false;
  }

  if (memchr(sv->str, '?', sv->length) != NULL) {
    return false;
  }

  //surpport only one pair
  if (findCharPair(sv, '(', ')', &start, &end)) {
    if (!(*(start + 1) == '[' && *(end - 2) == ']')) {
      return false;
    }

    if (memchr(end, '(', (sv->str + sv->length) - end) != NULL) {
      return false;
    }
  }

  if (!findCharPair(sv, '[', ']', &start, &end)) {
    return false;
  }

  //such as: [0-9] or [a-d]
  if (!((int)(end - start) == 5 && *(start + 2) == '-')) {
    return false;
  }

  //surpport only one pair
  if ( memchr(end, '[', (sv->str + sv->length) - end) != NULL) {
    return false;
  }

  if (memchr(sv->str, '\\', sv->length) != NULL) {
    const char *p;
    const char *pEnd;

    pEnd = sv->str + sv->length;
    for (p=sv->str; p<pEnd; p++) {
      if (*p == '\\' && *(p + 1) != '.') {
        return false;
      }
    }
  }

  return true;
}

int MappingManager::loadMappingUrls(MappingEntry *mappingEntry,
    const MappingParams *mappingParams)
{
  const StringValue * fromUrl = mappingParams->getFromUrl();
  const StringValue * toUrl = mappingParams->getToUrl();

  StringValue fromScheme;
  StringValue toScheme;

  this->getScheme(fromUrl, &fromScheme);
  if (!this->getScheme(toUrl, &toScheme)) {
    fprintf(stderr, "invalid to url: %.*s, expect scheme\n",
        toUrl->length, toUrl->str);
    return EINVAL;
  }

  char buff[4096];
  StringValue svUrl;
  StringValue fromHost;
  if (fromScheme.length == 0) {
    const SchemeParams *parentSchemeParams;
    if ((parentSchemeParams=dynamic_cast<const SchemeParams *>(
            mappingParams->getParent())) != NULL)
    {
      const StringValue *host = parentSchemeParams->getHost();
      if (16 + host->length + fromUrl->length >= (int)sizeof(buff)) {
        fprintf(stderr, "from url is too long, host length: %d, "
            "uri length: %d!\n", host->length, fromUrl->length);
        return ENOSPC;
      }

      fromScheme.str = parentSchemeParams->getScheme();
      fromScheme.length = strlen(fromScheme.str);

      svUrl.str = buff;
      svUrl.length = sprintf(buff, "%s://%.*s",
          fromScheme.str, host->length, host->str);
      if (*fromUrl->str != '/') {
        *(buff + svUrl.length++) = '/';
      }
      memcpy(buff + svUrl.length, fromUrl->str, fromUrl->length);
      svUrl.length += fromUrl->length;

      svUrl.strdup(&mappingEntry->_fromUrl);
      fromUrl = &svUrl;
      fromHost.str = fromUrl->str + fromScheme.length + 3;
      fromHost.length = host->length;
    }
    else {
      fromUrl->strdup(&mappingEntry->_fromUrl);
    }
  }
  else {
    if (!(fromScheme.equalsIgnoreCase(DIRECTVIE_NAME_HTTP,
            sizeof(DIRECTVIE_NAME_HTTP) - 1) || fromScheme.equalsIgnoreCase(
            DIRECTVIE_NAME_HTTPS, sizeof(DIRECTVIE_NAME_HTTPS) - 1) ||
          fromScheme.equalsIgnoreCase(DIRECTVIE_NAME_TUNNEL,
            sizeof(DIRECTVIE_NAME_TUNNEL) - 1)))
    {
      fprintf(stderr, "invalid scheme: %.*s of from url: %.*s\n",
          fromScheme.length, fromScheme.str, fromUrl->length, fromUrl->str);
      return EINVAL;
    }

    fromUrl->strdup(&mappingEntry->_fromUrl);

    fromHost.str = fromUrl->str + fromScheme.length + 3;  //3 for skip ://
    const char *hostEnd = strchr(fromHost.str, '/');
    if (hostEnd != NULL) {
      fromHost.length = hostEnd - fromHost.str;
    }
    else {
      fromHost.length = (fromUrl->str + fromUrl->length) - fromHost.str;
    }
  }

  if (!(toScheme.equalsIgnoreCase(DIRECTVIE_NAME_HTTP,
        sizeof(DIRECTVIE_NAME_HTTP) - 1) || toScheme.equalsIgnoreCase(
        DIRECTVIE_NAME_HTTPS, sizeof(DIRECTVIE_NAME_HTTPS) - 1) ||
        toScheme.equalsIgnoreCase(DIRECTVIE_NAME_TUNNEL,
          sizeof(DIRECTVIE_NAME_TUNNEL) - 1)))
  {
    fprintf(stderr, "invalid scheme: %.*s of to url: %.*s\n",
        toScheme.length, toScheme.str, toUrl->length, toUrl->str);
    return EINVAL;
  }

  //printf("from host: %.*s\n", fromHost.length, fromHost.str);

  toUrl->strdup(&mappingEntry->_toUrl);

  if (fromHost.length > 0 && isRegex(&fromHost)) {
    //printf("%.*s is REGEX!\n", mappingEntry->_fromUrl.length, mappingEntry->_fromUrl.str);
    mappingEntry->_flags |= MAPPING_FLAG_REGEX;
    mappingEntry->_simpleRegexRange = this->isRegexSimpleRange(&fromHost);
    if (*(fromHost.str + fromHost.length - 1) == '$') {
      const char *pColon = (const char *)memchr(fromHost.str, ':', fromHost.length);
      if (pColon != NULL) {  //such as example.com:8080$, remove the $
        int urlLength;
        int remainLen;
        urlLength = sprintf((char *)mappingEntry->_fromUrl.str, "%.*s://%.*s",
            fromScheme.length, fromScheme.str, fromHost.length - 1, fromHost.str);

        remainLen = fromUrl->length - (urlLength + 1);
        if (remainLen > 0) {
          memcpy((char *)mappingEntry->_fromUrl.str + urlLength,
              fromUrl->str + urlLength + 1, remainLen);
          urlLength += remainLen;
          *((char *)mappingEntry->_fromUrl.str + urlLength) = '\0';
        }

        mappingEntry->_fromUrl.length = urlLength;
      }
    }
  }

  return 0;
}

int MappingManager::loadPlugins(MappingEntry *mappingEntry,
    const MappingParams *mappingParams)
{
  int result;
  const PluginParams *pluginParams;
  const DirectiveParams *children = mappingParams->getChildren();
  while (children != NULL) {
    if ((pluginParams=dynamic_cast<const PluginParams *>(
            children)) != NULL)
    {
      if ((result=((PluginParams *)pluginParams)->combineParams()) != 0) {
        return result;
      }

      const PluginInfo *pluginInfo = pluginParams->getPluginInfo();
      if (!mappingEntry->_plugins.checkSize()) {
        return ENOMEM;
      }

      if (pluginInfo->duplicate(mappingEntry->_plugins.items +
            mappingEntry->_plugins.count) == NULL)
      {
        return ENOMEM;
      }
      mappingEntry->_plugins.count++;
    }

    children = children->next();
  }

  return 0;
}

int MappingManager::addConfig(DynamicArray<ConfigKeyValue> *config,
    const ConfigKeyValue *configKV)
{
  if (!config->checkSize()) {
    return ENOMEM;
  }

  if (configKV->duplicate(config->items + config->count) == NULL) {
    return ENOMEM;
  }
  config->count++;
  return 0;
}

int MappingManager::loadConfig(MappingEntry *mappingEntry,
    const ConfigParams *configParams)
{
  int index;
  switch (configParams->getConfigType()) {
    case CONFIG_TYPE_RECORDS_INT:
      index = CONFIG_TYPE_RECORDS_INDEX;
      break;
    case CONFIG_TYPE_HOSTING_INT:
      index = CONFIG_TYPE_HOSTING_INDEX;
      break;
    case CONFIG_TYPE_CACHE_CONTROL_INT:
      index = CONFIG_TYPE_CACHE_CONTROL_INDEX;
      break;
    case CONFIG_TYPE_CONGESTION_INT:
      index = CONFIG_TYPE_CONGESTION_INDEX;
      break;
    default:
      return EINVAL;
  }

  DynamicArray<ConfigKeyValue> *config = mappingEntry->_configs + index;
  if (!configParams->isBlock()) {
    return this->addConfig(config, configParams->getConfig());
  }
  else {
    int result;
    const ConfigSetParams *setParams;
    const DirectiveParams *children = configParams->getChildren();
    while (children != NULL) {
      if ((setParams=dynamic_cast<const ConfigSetParams *>(
              children)) != NULL)
      {
        if ((result=this->addConfig(config, setParams->getConfig())) != 0) {
          return result;
        }
      }

      children = children->next();
    }

    return 0;
  }
}

int MappingManager::loadConfigs(MappingEntry *mappingEntry,
    const MappingParams *mappingParams)
{
  int result;
  const ConfigParams *configParams;
  const DirectiveParams *children = mappingParams->getChildren();
  while (children != NULL) {
    if ((configParams=dynamic_cast<const ConfigParams *>(
            children)) != NULL)
    {
      if ((result=this->loadConfig(mappingEntry, configParams)) != 0) {
        return result;
      }
    }

    children = children->next();
  }

  return 0;
}

void MappingManager::addCheckLists(MappingEntry *mappingEntry,
    ACLCheckListContainer *checkLists)
{
  if (checkLists->methodIpCheckList != NULL) {
    mappingEntry->_aclMethodIpCheckLists.add(checkLists->methodIpCheckList);
  }

  if (checkLists->refererCheckList != NULL) {
    mappingEntry->_aclRefererCheckLists.add(checkLists->refererCheckList);
  }
}

int MappingManager::loadMapping(const MappingParams *mappingParams,
    ACLCheckListContainer *parentCheckLists)
{
  int result;
  ACLCheckListContainer checkLists;
  if ((result=this->loadCheckLists(mappingParams, &checkLists)) != 0) {
    return result;
  }

  MappingEntry *mappingEntry = new MappingEntry(
      mappingParams->getLineInfo()->lineNo, mappingParams->getType(),
      mappingParams->getFlags());
  if (mappingEntry == NULL) {
    return ENOMEM;
  }

  if (parentCheckLists != NULL) {
    this->addCheckLists(mappingEntry, parentCheckLists);
  }

  this->addCheckLists(mappingEntry, &checkLists);

  do {
    if ((result=loadMappingUrls(mappingEntry, mappingParams)) != 0) {
      break;
    }

    if ((result=loadPlugins(mappingEntry, mappingParams)) != 0) {
      break;
    }

    if ((result=loadConfigs(mappingEntry, mappingParams)) != 0) {
      break;
    }

    result = _mappings.add(mappingEntry) ?  0 : ENOMEM;
  } while (0);

  if (result != 0) {
    delete mappingEntry;
  }

  return result;
}

int MappingManager::getHostname(const StringValue *url,
    const char **start, const char **end)
{
    *start = strstr(url->str, "//");
    if (*start == NULL) {
      *end = NULL;
      return EINVAL;
    }

    *start += 2;
    *end = (const char *)memchr(*start, '/',
        (url->str + url->length) - *start);
    if (*end == NULL) {
      *end = url->str + url->length;
    }

    return 0;
}

int MappingManager::expand()
{
  if (_mappings.count == 0) {
    return 0;
  }

  MappingEntry **mappingEntry;
  MappingEntry **mappingEnd;

  mappingEnd = _mappings.items + _mappings.count;
  for (mappingEntry=_mappings.items; mappingEntry<mappingEnd; mappingEntry++) {
    if ((*mappingEntry)->_simpleRegexRange) {
      break;
    }
  }

  if (mappingEntry == mappingEnd) {  //no simple regex range
    return 0;
  }

  int result;
  int ch;
  char chStart, chEnd;
  const char *rangeStart, *rangeEnd;
  MappingEntry *newEntry;
  char *newFromUrl;
  char *newToUrl;
  const char *fromHostStart, *fromHostEnd;
  const char *toHostStart, *toHostEnd;
  bool haveGroup;
  DynamicArray<MappingEntry *> oldMappings(_mappings);

  _mappings.reset(2 * oldMappings.count);

  for (mappingEntry=oldMappings.items; mappingEntry<mappingEnd; mappingEntry++) {
    if (!(*mappingEntry)->_simpleRegexRange) {
      result = _mappings.add(*mappingEntry) ?  0 : ENOMEM;
      if (result != 0) {
        return result;
      }

      continue;
    }

    StringValue fromUrl((*mappingEntry)->_fromUrl);
    StringValue toUrl((*mappingEntry)->_toUrl);

    if (this->getHostname(&fromUrl, &fromHostStart, &fromHostEnd) != 0) {
      result = _mappings.add(*mappingEntry) ?  0 : ENOMEM;
      if (result != 0) {
        return result;
      }

      continue;
    }

    int fromHostLen = fromHostEnd - fromHostStart;
    //convert regex escaped char \. to .
    if (*fromHostStart == '^' || memchr(fromHostStart, '$', fromHostLen)
        != NULL || memchr(fromHostStart, '\\', fromHostLen) != NULL)
    {
      int urlLen;
      char urlBuff[4096];

      if (fromUrl.length >= (int)sizeof(urlBuff)) {
        fprintf(stderr, "from url tool long, exceeds %d!\n",
            (int)sizeof(urlBuff));
        result = _mappings.add(*mappingEntry) ?  0 : ENOMEM;
        if (result != 0) {
          return result;
        }

        continue;
      }

      urlLen = sprintf(urlBuff, "%.*s",
          (int)(fromHostStart - fromUrl.str), fromUrl.str);

      int startOffset = *fromHostStart == '^' ? 1 : 0;   //skip start char ^
      const char *pSrc;
      char *pDest;
      pDest = urlBuff + urlLen;
      for (pSrc=fromHostStart+startOffset; pSrc<fromHostEnd; pSrc++) {
        if (*pSrc == '\\' && *(pSrc + 1) == '.') {
          *pDest++ = *(++pSrc);
        }
        else if (*pSrc != '$') {  //skip the $
          *pDest++ = *pSrc;
        }
      }

      urlLen = pDest - urlBuff;
      urlLen += sprintf(pDest, "%s", fromHostEnd);

      memcpy((char *)fromUrl.str, urlBuff, urlLen + 1);
      fromUrl.length = urlLen;
    }

    rangeStart = (const char *)memchr(fromUrl.str, '[', fromUrl.length);
    if (rangeStart == NULL) {
      result = _mappings.add(*mappingEntry) ?  0 : ENOMEM;
      if (result != 0) {
        return result;
      }

      continue;
    }
    rangeEnd = rangeStart + 5;

    chStart = *(rangeStart + 1);
    chEnd = *(rangeStart + 3);
    if (chStart > chEnd) {
      fprintf(stderr, "invalid char range from %c to %c",
          chStart, chEnd);

      result = _mappings.add(*mappingEntry) ?  0 : ENOMEM;
      if (result != 0) {
        return result;
      }

      continue;
    }

    if (rangeStart > fromUrl.str && *(rangeStart - 1) == '(')
    {
      rangeStart--;
      rangeEnd++;
      haveGroup = true;
    }
    else {
      haveGroup = false;
    }

    if (this->getHostname(&toUrl, &toHostStart, &toHostEnd) != 0) {
      result = _mappings.add(*mappingEntry) ?  0 : ENOMEM;
      if (result != 0) {
        return result;
      }

      continue;
    }

    (*mappingEntry)->_flags &= ~MAPPING_FLAG_REGEX;  //remove the regex flag

    int frontLen = rangeStart - fromUrl.str;
    int tailLen = (fromUrl.str + fromUrl.length) - rangeEnd;
    for (ch=chStart; ch<=chEnd; ch++) {
      if (ch == chStart) {
        newEntry = *mappingEntry;
      }
      else {
        newEntry = new MappingEntry(**mappingEntry);
      }

      newEntry->_fromUrl.length = frontLen + 1 + tailLen;
      newFromUrl = (char *)malloc(newEntry->_fromUrl.length + 1);
      if (newFromUrl == NULL) {
        result = errno != 0 ? errno : ENOMEM;
        fprintf(stderr, "malloc %d bytes fail, error info: %s",
            newEntry->_fromUrl.length + 1, strerror(result));
        return result;
      }
      newEntry->_fromUrl.str = (const char *)newFromUrl;
      sprintf(newFromUrl, "%.*s%c%s", frontLen, fromUrl.str, ch, rangeEnd);

      if (memchr(toHostStart, '$', toHostEnd - toHostStart) == NULL) {
        toUrl.strdup(&newEntry->_toUrl);
      }
      else {
        int allocSize;
        allocSize = toUrl.length + 1;
        newToUrl = (char *)malloc(allocSize);
        if (newToUrl == NULL) {
          result = errno != 0 ? errno : ENOMEM;
          fprintf(stderr, "malloc %d bytes fail, error info: %s",
              allocSize, strerror(result));
          return result;
        }

        const char *pSrc;
        char *pDest;
        pDest = newToUrl;

        for (pSrc=toUrl.str; pSrc<toHostEnd; pSrc++) {
          if (*pSrc != '$') {
            *pDest++ = *pSrc;
            continue;
          }

          if (*(pSrc + 1) == '1') {  //$1
            if (!haveGroup) {
              fprintf(stderr, "inlivad regex group number: $1\n");
              return EINVAL;
            }

            *pDest++ = ch;
            pSrc++;  //skip $
          }
          else if (*(pSrc + 1) == '0') { //$0
            const char *fromHostStart;
            const char *fromHostEnd;
            int fromHostLen;

            this->getHostname(&newEntry->_fromUrl,
                &fromHostStart, &fromHostEnd);
            fromHostLen = fromHostEnd - fromHostStart;
            allocSize += fromHostLen;
            newToUrl = (char *)realloc(newToUrl, allocSize);
            if (newToUrl == NULL) {
              result = errno != 0 ? errno : ENOMEM;
              fprintf(stderr, "malloc %d bytes fail, error info: %s",
                  allocSize, strerror(result));
              return result;
            }

            memcpy(pDest, fromHostStart, fromHostLen);
            pDest += fromHostLen;
            pSrc++;  //skip $
          }
          else {
            fprintf(stderr, "invalid regex group number: %c\n", *(pSrc + 1));
            return EINVAL;
          }
        }

        newEntry->_toUrl.str = (const char *)newToUrl;
        newEntry->_toUrl.length = pDest - newToUrl;
        newEntry->_toUrl.length += sprintf(pDest, "%s", toHostEnd);
      }

      result = _mappings.add(newEntry) ?  0 : ENOMEM;
      if (result != 0) {
        return result;
      }
    }

    fromUrl.free();
    toUrl.free();
  }

  return 0;
}

