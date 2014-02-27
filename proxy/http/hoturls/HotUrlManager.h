#ifndef _HOT_URL_MANAGER_H
#define _HOT_URL_MANAGER_H

#include "I_HotUrls.h"
#include "HotUrlMap.h"

class HotUrlManager
{

  struct HotUrlEntry : public UrlEntry {
    unsigned int generation;
    int cache_flag;
  };

  class UrlArray
  {
    public:
      UrlArray();
      ~UrlArray();

      inline int add(const UrlEntry *entry, const bool checkDuplicate = true) {
        return add(entry->url, entry->length, checkDuplicate);
      }

      int add(const char *url, const int length, const bool checkDuplicate = true);

      int replace(const HotUrlMap::UrlMapEntry *head,
          const HotUrlMap::UrlMapEntry *tail);

      inline void clear() {
        _count = 0;
      }

      inline bool contains(const UrlEntry *entry) {
        return contains(entry->url, entry->length);
      }

      inline bool contains(const char *url, const int length) {
        return find(url, length) != NULL;
      }

      inline HotUrlEntry *find(const UrlEntry *entry) {
        return find(entry->url, entry->length);
      }

      HotUrlEntry *find(const char *url, const int length);

      inline int getCount() {
        return _count;
      }

      inline UrlEntry *getUrls() {
        return _urls;
      }

    private:
      void clearOldEntries();

      HotUrlEntry *_urls;
      int _allocSize;
      int _count;
      unsigned int _generation;
  };

  public:
    ~HotUrlManager() {}

    static inline int getCacheControl(const char *url, const int length) {
      HotUrlEntry *entry = instance->_currentHotUrls.find(url, length);
      return entry == NULL ? CACHE_CONTROL_CLUSTER : entry->cache_flag;
    }

    static inline int getHotUrlCount() {
      return instance->_currentHotUrls.getCount();
    }

    static inline UrlArray *getHotUrlArray() {
      return &instance->_currentHotUrls;
    }

    static void migrateFinish(const char *url, const int length);

  private:
    HotUrlManager() {}

    // prevent unauthorized copies (Not implemented)
    HotUrlManager(const HotUrlManager &);
    HotUrlManager& operator=(const HotUrlManager &);

    static HotUrlManager *instance;
    UrlArray _currentHotUrls;
};

#endif

