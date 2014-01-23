/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

//
#include "I_HotUrls.h"

#ifndef _HOT_URL_MAP_H_
#define _HOT_URL_MAP_H_

#define URL_HASH_TABLE_SIZE  10949
#define MAX_URL_COUNT_PER_BUCKET  4

class HotUrlMap
{
  public:
    struct UrlMapEntry {
      UrlEntry _url;
      int _count;     //access count
      bool _inQueue;
      int64_t _bytes;
      UrlMapEntry *_next;  //for priority queue
      UrlMapEntry *_prev;  //for priority queue

      inline bool equals(const UrlEntry *url) {
        return _url.equals(url);
      }

      inline bool equals(const char *url, const int url_len) {
        return _url.equals(url, url_len);
      }

      inline void setUrl(const char *url, const int url_len) {
        memcpy(_url.url, url, url_len);
        *(_url.url + url_len) = '\0';
        _url.length = url_len;
      }
    };

    class PriorityQueue {
      public:
        PriorityQueue();
        ~PriorityQueue();

        inline const UrlMapEntry *head() const {
          return _head;
        }

        inline void clear() {
          ink_mutex_acquire(&_mutex);
          _count = 0;
          _head = NULL;
          _tail = NULL;
          _minBytes = 0;
          ink_mutex_release(&_mutex);
        }

        void setMaxCount(const uint32_t maxCount) {
          _maxCount = maxCount;
        }

        void add(UrlMapEntry *entry);

      private:
        uint32_t _maxCount;
        uint32_t _count;
        UrlMapEntry *_head;  //the largest
        UrlMapEntry *_tail;  //the smallest
        int64_t _minBytes;
        ink_mutex _mutex;
    };

    struct UrlHashBucket {
      ink_mutex _mutex;  //use for locking this bucket
      UrlMapEntry _urls[MAX_URL_COUNT_PER_BUCKET]; //urls items
    };

    struct UrlHashTable {
      UrlHashBucket *_buckets;
      int *_counts;
    };

  public:
    HotUrlMap();

    inline PriorityQueue *getUrlQueue() {
      return &_urlQueue;
    }

    inline void clear() {
      memset(_urlTable._counts, 0, sizeof(int) * URL_HASH_TABLE_SIZE);
      _urlQueue.clear();
    }


    /**
     * Gets the send bytes for the url
     * @param url the url
     * @param url_len the url length
     * @return send bytes
     */
    int64_t getBytes(const char *url, const int url_len);

    /**
     * Change (increment/decrement) the send bytes
     * @param url the url
     * @param url_len the url length
     * @param bytes to increase
     */
    void incrementBytes(const char *url, const int url_len, const int64_t bytes);

    /**
     * stat url hash table
     * @param urlCount the url count
     * @param min the min url count of a bucket
     * @param max the max url count of a bucket
     * @param avg the avg url count
     */
    void urlTableStat(int &urlCount, int &min, int &max, double &avg);

  protected:
    inline uint64_t time33Hash(const char *key, const int key_len)
    {
      uint64_t nHash;
      unsigned char *pKey;
      unsigned char *pEnd;

      nHash = 0;
      pEnd = (unsigned char *)key + key_len;
      for (pKey = (unsigned char *)key; pKey < pEnd; pKey++) {
        nHash += (nHash << 5) + (*pKey);
      }
      return nHash;
    }

    inline UrlMapEntry *find(const unsigned int index, const char *url, const int url_len)
    {
      UrlMapEntry *urls = _urlTable._buckets[index]._urls;
      int count = _urlTable._counts[index];

      for (int i=0; i<count; i++) {
        if (urls[i].equals(url, url_len)) {
          return urls  + i;
        }
      }
      return NULL;
    }

  private:
    // Hide the constructor and copy constructor
    HotUrlMap(const HotUrlMap & x) { NOWARN_UNUSED(x); }

    UrlHashTable _urlTable;
    PriorityQueue _urlQueue;
};

#endif

