#include "HotUrlStats.h"
#include "HotUrlManager.h"
#include "HotUrlHistory.h"

unsigned int HotUrlManager::UrlArray::_generation = 0;

HotUrlManager::UrlArray::UrlArray()
  : _urls(NULL),
  _allocSize(0),
  _count(0)
{
}

HotUrlManager::UrlArray::~UrlArray()
{
  if (_urls != NULL) {
    ats_free(_urls);
    _urls = NULL;
  }
  _allocSize = 0;
  _count = 0;
}

int HotUrlManager::UrlArray::add(const char *url, const int length, const bool checkDuplicate)
{
  if (checkDuplicate) {
    if (contains(url, length)) {
      return EEXIST;
    }
  }

  if (length >= MAX_URL_SIZE) {
    return ENAMETOOLONG;
  }

  if (_count >= _allocSize) {
    int bytes;
    int allocSize = _allocSize == 0 ? 32 : 2 *_allocSize;
    bytes = sizeof(HotUrlEntry) * allocSize;
    HotUrlEntry *urls = (HotUrlEntry *)ats_realloc(_urls, bytes);
    if (urls == NULL) {
      return ENOMEM;
    }

    _urls = urls;
    _allocSize = allocSize;
  }

  HotUrlEntry *entry = _urls + _count++;
  memcpy(entry->url, url, length);
  entry->length = length;
  entry->generation = _generation;

  ink_mutex_acquire(&instance->_mutex);
  HotUrlHistory::HotUrlEntry *historyEntry = HotUrlHistory::getHotUrl(url, length);
  if (historyEntry != NULL) {
    entry->cache_flag = CACHE_CONTROL_LOCAL;
    time_t createTime  = (time_t)(ink_get_hrtime() / HRTIME_SECOND);
    if (historyEntry->createTime < createTime) {
      historyEntry->createTime = createTime;
    }
  }
  else {
    entry->cache_flag = CACHE_CONTROL_MIGRATE;
  }
  ink_mutex_release(&instance->_mutex);

  Debug(HOT_URLS_DEBUG_TAG, "[generation=%u] %d. new hot url: %.*s, cache_flag: %d",
        _generation, _count, length, url, entry->cache_flag);
  return 0;
}

HotUrlManager::HotUrlEntry *HotUrlManager::UrlArray::find(const char *url, const int length)
{
  HotUrlEntry *entry;
  HotUrlEntry *entryEnd;

  if (_count == 0) {
    return NULL;
  }

  entryEnd = _urls + _count;
  for (entry=_urls; entry<entryEnd; entry++) {
    if (entry->equals(url, length)) {
      return entry;
    }
  }

  return NULL;
}

void HotUrlManager::UrlArray::clearOldEntries()
{
  HotUrlEntry *dest;
  HotUrlEntry *entry;
  HotUrlEntry *entryEnd;

  dest = _urls;
  entryEnd = _urls + _count;
  for (entry=_urls; entry<entryEnd; entry++) {
    if (entry->generation == _generation ||
        entry->cache_flag == CACHE_CONTROL_MIGRATE)
    {
      if (dest != entry) {
        memcpy(dest->url, entry->url, entry->length);
        dest->length = entry->length;
        dest->generation = entry->generation;
      }
      dest++;
    }
  }

  _count = dest - _urls;
}

void HotUrlManager::UrlArray::replace(const HotUrlMap::UrlMapEntry *head,
    const HotUrlMap::UrlMapEntry *tail)
{
  const HotUrlMap::UrlMapEntry *eofEntry = tail->_next;

  ++_generation;
  if (_count == 0) {
    while (head != eofEntry) {
      add(&head->_url, false);
      head = head->_next;
    }
    return;
  }

  int replaceCount = 0;
  int oldCount = _count;
  HotUrlManager::HotUrlEntry *found;
  while (head != eofEntry) {
    found = find(&head->_url);
    if (found != NULL) {
      found->generation = _generation;
      ++replaceCount;
    }
    else {
      add(&head->_url, false);
    }

    head = head->_next;
  }

  if (replaceCount == oldCount) {  //all be replaced
    return;
  }

  clearOldEntries();
  return;
}

HotUrlManager::HotUrlManager()
{
  ink_mutex_init(&_mutex, "HotUrlManager");
  _currentHotUrls = _hotUrls + 0;
}

void HotUrlManager::migrateFinish(const char *url, const int length)
{
  HotUrlEntry *entry;
  int old_cache_flag;

  ink_mutex_acquire(&instance->_mutex);
  entry = instance->_currentHotUrls->find(url, length);
  if (entry != NULL) {
    old_cache_flag = entry->cache_flag;
    entry->cache_flag = CACHE_CONTROL_LOCAL;
  }
  else {
    old_cache_flag = CACHE_CONTROL_CLUSTER;
  }

  time_t createTime  = (time_t)(ink_get_hrtime() / HRTIME_SECOND);
  HotUrlHistory::add(url, length, createTime);
  ink_mutex_release(&instance->_mutex);

  Debug(HOT_URLS_DEBUG_TAG, "migrateFinish, url: %.*s, old cache flag: %d", length, url, old_cache_flag);
}

HotUrlManager *HotUrlManager::instance = new HotUrlManager;

