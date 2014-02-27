#ifndef _HOT_URL_HISTORY_H
#define _HOT_URL_HISTORY_H

#include "I_HotUrls.h"
#include "HotUrlMap.h"

class HotUrlHistory
{
  public:
    struct HotUrlEntry : public UrlEntry {
      time_t createTime;

      static int compare(const void *p1, const void *p2) {
        HotUrlEntry *entry1 = (HotUrlEntry *)p1;
        HotUrlEntry *entry2 = (HotUrlEntry *)p2;

        if (entry1->length > entry2->length) {
          return 1;
        }
        else if (entry1->length < entry2->length) {
          return -1;
        }
        else {
          return memcmp(entry1->url, entry2->url, entry1->length);
        }
      }
    };

  protected:
    class UrlArray
    {
      public:
        UrlArray();
        ~UrlArray();

        inline int add(const UrlEntry *entry, const time_t createTime) {
          return add(entry->url, entry->length, createTime);
        }

        int add(const char *url, const int length, const time_t createTime);

        inline bool contains(const UrlEntry *entry) {
          return contains(entry->url, entry->length);
        }

        inline bool contains(const char *url, const int length) {
          return find(url, length) != NULL;
        }

        HotUrlEntry *equalLarge(HotUrlEntry *target, bool *bEqual);

        inline HotUrlEntry *find(const UrlEntry *entry) {
          return find(entry->url, entry->length);
        }

        HotUrlEntry *find(const char *url, const int length);

        inline void clear() {
          _count = 0;
        }

        inline void sort() {
          if (_count <= 1) {
            return;
          }

          qsort(_urls, _count, sizeof(HotUrlEntry), HotUrlEntry::compare);
        }

        inline int getCount() {
          return _count;
        }

        inline HotUrlEntry *getUrls() {
          return _urls;
        }

      private:
        HotUrlEntry *_urls;  //sorted
        int _allocSize;
        int _count;
    };

  public:
    ~HotUrlHistory() {}

    static inline bool isHotUrl(const char *url, const int length) {
      return instance->_currentHotUrls.contains(url, length);
    }

    static inline HotUrlEntry *getHotUrl(const char *url, const int length) {
      return instance->_currentHotUrls.find(url, length);
    }

    static inline int getHotUrlCount() {
      return instance->_currentHotUrls.getCount();
    }

    static inline UrlArray *getHotUrlArray() {
      return &instance->_currentHotUrls;
    }

    static inline int add(const UrlEntry *entry, const time_t createTime) {
      return add(entry->url, entry->length, createTime);
    }

    static inline int add(const char *url, const int length,
        const time_t createTime) {
      return instance->doAdd(url, length, createTime);
    }

    static inline int load(const char *filename) {
      snprintf(instance->_filename, sizeof(instance->_filename),
          "%s", filename);
      return instance->doLoad();
    }

    static inline int reload() {
      return instance->doReload();
    }

    int doAdd(const char *url, const int length, const time_t createTime);

    int doLoad();

    int doReload();

  private:
    HotUrlHistory() : _fd(-1) {
    }

    int doDump();
    int openBinlog();

    void removeCache(HdrHeap *hdrHeap, const char *url, const int length);

    // prevent unauthorized copies (Not implemented)
    HotUrlHistory(const HotUrlHistory &);
    HotUrlHistory& operator=(const HotUrlHistory &);

    static HotUrlHistory *instance;
    UrlArray _currentHotUrls;
    char _filename[256];
    int _fd;
};

#endif

