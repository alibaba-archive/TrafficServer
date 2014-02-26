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

#include "HotUrlMap.h"
#include "HotUrlStats.h"

#define CLEAR_INTERVAL (100 * HRTIME_MSECOND)

inline int64_t HotUrlMap::UrlMapEntry::getOrderBy()
{
  return (HotUrlStats::getDetecType() & HOT_URLS_DETECT_TYPE_BYTES) ?
    _bytes : _count;
}

HotUrlMap::PriorityQueue::PriorityQueue()
  : _maxCount(0), _count(0), _head(NULL), _tail(NULL), _min(0)
{
  ink_mutex_init(&_mutex, "HotUrlPriorityQueue");
}

HotUrlMap::PriorityQueue::~PriorityQueue()
{
  ink_mutex_destroy(&_mutex);
}

void HotUrlMap::PriorityQueue::add(UrlMapEntry *entry)
{
  if (entry->getOrderBy() <= _min) {
    return;
  }

  ink_mutex_acquire(&_mutex);
  do {
    if (entry->getOrderBy() <= _min) {
      break;
    }

    if (!entry->_inQueue) {
      entry->_inQueue = true;
      if (_count < _maxCount) {
        ++_count;
      }
      else if (_tail != NULL) {  //remove the tail node
        _tail->_inQueue = false;
        if (_tail->_prev != NULL) {
          _tail->_prev->_next = NULL;
          _tail = _tail->_prev;
        }
        else {
          _head = NULL;
          _tail = NULL;
        }
      }

      entry->_prev = _tail;
      entry->_next = NULL;
      if (_tail == NULL) {
        _head = entry;
      }
      else {
        _tail->_next = entry;
      }

      _tail = entry;
      _min = entry->getOrderBy();
    }
    else
    {
      if (entry->_next == NULL) {  //i am the tail node
        _min = entry->getOrderBy();
      }
    }

    if (entry->_prev == NULL) { //i am the largest, do NOT resort
      break;
    }

    if (entry->getOrderBy() <= entry->_prev->getOrderBy()) { //order is OK
      break;
    }

    //remove this entry first
    if (entry->_next == NULL) { //i am the tail node
      _tail = entry->_prev;
      _tail->_next = NULL;
      _min = _tail->getOrderBy();
    }
    else {
      entry->_next->_prev = entry->_prev;
      entry->_prev->_next = entry->_next;
    }

    //then insert this entry
    UrlMapEntry *prev = entry->_prev->_prev;
    while (prev != NULL && entry->getOrderBy() > prev->getOrderBy()) {
      prev = prev->_prev;
    }
    if (prev == NULL) {
      entry->_prev = NULL;
      entry->_next = _head;
      _head->_prev = entry;
      _head = entry;
    }
    else {
      prev->_next->_prev = entry;
      entry->_next = prev->_next;
      entry->_prev = prev;
      prev->_next = entry;
    }
  } while (0);
  ink_mutex_release(&_mutex);
}

HotUrlMap::HotUrlMap()
{
  int64_t bytes;
  UrlHashBucket *pBucket;
  UrlHashBucket *pEnd;

  bytes = sizeof(UrlHashBucket) * URL_HASH_TABLE_SIZE;
  _urlTable._buckets = (UrlHashBucket *)ats_malloc(bytes);

  bytes = sizeof(int) * URL_HASH_TABLE_SIZE;
  _urlTable._counts = (int *)ats_malloc(bytes);
  memset(_urlTable._counts, 0, bytes);

  pEnd = _urlTable._buckets + URL_HASH_TABLE_SIZE;
  for (pBucket=_urlTable._buckets; pBucket<pEnd; pBucket++) {
    ink_mutex_init(&pBucket->_mutex, "HotUrlMapMutex");
  }
}

const HotUrlMap::UrlMapEntry *HotUrlMap::get(const char *url, const int url_len)
{
  unsigned int index;
  UrlMapEntry *found;

  index = time33Hash(url, url_len) % URL_HASH_TABLE_SIZE;
  ink_mutex_acquire(&(_urlTable._buckets[index]._mutex));
  found = find(index, url, url_len);
  ink_mutex_release(&(_urlTable._buckets[index]._mutex));
  return found;
}

void HotUrlMap::incrementBytes(const char *url, const int url_len, const int64_t bytes)
{
  unsigned int index;
  UrlMapEntry *found;

  if (url_len >= MAX_URL_SIZE) {
    return;
  }

  index = time33Hash(url, url_len) % URL_HASH_TABLE_SIZE;
  ink_mutex_acquire(&(_urlTable._buckets[index]._mutex));
  found = find(index, url, url_len);
  if (found != NULL) {
    found->_bytes += bytes;
    found->_count += 1;
  }
  else {
    if (_urlTable._counts[index] >= MAX_URL_COUNT_PER_BUCKET) {
      ink_mutex_release(&(_urlTable._buckets[index]._mutex));
      return;
    }

    found = _urlTable._buckets[index]._urls + _urlTable._counts[index];
    found->_inQueue = false;
    found->_bytes = bytes;
    found->_count = 1;
    found->setUrl(url, url_len);
    _urlTable._counts[index]++;
  }
  ink_mutex_release(&(_urlTable._buckets[index]._mutex));

  _urlQueue.add(found);
  return;
}

void HotUrlMap::urlTableStat(int &urlCount, int &min, int &max, double &avg)
{
  int *pCount;
  int *pEnd;
  int usedCount;

  urlCount = 0;
  usedCount = 0;
  min = -1;
  max = 0;
  pEnd = _urlTable._counts + URL_HASH_TABLE_SIZE;
  for (pCount=_urlTable._counts; pCount<pEnd; pCount++) {
    if (*pCount > 0) {
      usedCount++;
      urlCount += *pCount;

      if (*pCount > max) {
        max = *pCount;
      }
    }

    if (min < 0) {
      min = *pCount;
    }
    else if (*pCount < min) {
      min = *pCount;
    }
  }
  if (usedCount > 0) {
    avg = (double)urlCount / (double)usedCount;
  }
  else {
    avg = 0.00;
  }
}

