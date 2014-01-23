#include "HotUrlStats.h"
#include "HotUrlManager.h"

HotUrlManager::UrlArray::UrlArray()
  : _urls(NULL),
  _allocSize(0),
  _count(0),
  _generation(0)
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
    int allocSize = _allocSize == 0 ? 8 : 2 *_allocSize;
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

  Debug(HOT_URLS_DEBUG_TAG, "[generation=%u] %d. new hot url: %.*s",
        _generation, _count, length, url);
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
    if (entry->generation == _generation) {
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

int HotUrlManager::UrlArray::replace(const HotUrlMap::UrlMapEntry *head,
    const HotUrlMap::UrlMapEntry *tail)
{
  const HotUrlMap::UrlMapEntry *eofEntry = tail->_next;

  ++_generation;
  if (_count == 0) {
    while (head != eofEntry) {
      add(&head->_url, false);
      head = head->_next;
    }
    return 0;
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
    return 0;
  }

  clearOldEntries();
  return 0;
}

HotUrlManager *HotUrlManager::instance = new HotUrlManager;

