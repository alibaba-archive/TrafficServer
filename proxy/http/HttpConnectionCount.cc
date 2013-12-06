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

#include "I_EventSystem.h"
#include "HttpConnectionCount.h"

#define CLEAR_INTERVAL (100 * HRTIME_MSECOND)

ConnectionCount ConnectionCount::_connectionCount;

ConnectionCount::ConnectionCount() : _hostCount(0)
{
  int bytes;
  HostHashBucket *pBucket;
  HostHashBucket *pEnd;

  bytes = sizeof(HostHashBucket) * HOST_HASH_TABLE_SIZE;
  _hostTable = (HostHashBucket *)ats_malloc(bytes);
  memset(_hostTable, 0, bytes);

  pEnd = _hostTable + HOST_HASH_TABLE_SIZE;
  for (pBucket=_hostTable; pBucket<pEnd; pBucket++) {
    ink_mutex_init(&pBucket->_mutex, "ConnectionCountMutex");
  }
}

int ConnectionCount::clear(HostHashBucket *pBucket)
{
  HostEntry *newHosts;
  HostEntry *srcEnd;
  HostEntry *pSrcHost;
  HostEntry *pDestHost;
  HostEntry *tmp;
  int oldCount;
  int clearCount;

  pBucket->last_clear_time = ink_get_hrtime();
  newHosts = (HostEntry *)ats_malloc(sizeof(HostEntry) *
      pBucket->_allocSize);
  srcEnd = pBucket->_hosts + pBucket->_count;
  pDestHost = newHosts;
  for (pSrcHost=pBucket->_hosts; pSrcHost<srcEnd; pSrcHost++) {
    if (pSrcHost->_count > 0) {  //should keep
      pDestHost->_hostname = pSrcHost->_hostname;
      pDestHost->_length = pSrcHost->_length;
      pDestHost->_count = pSrcHost->_count;
      pDestHost++;
    }
    else {  //should release
      pSrcHost->_length = 0;
      ats_free(pSrcHost->_hostname);
      pSrcHost->_hostname = NULL;
    }
  }

  oldCount = pBucket->_count;
  tmp = pBucket->_hosts;
  pBucket->_count = pDestHost - newHosts;
  pBucket->_hosts = newHosts;
  ats_free(tmp);
  clearCount = oldCount - pBucket->_count;
  if (clearCount == 0) {
    return 0;
  }

  ink_atomic_increment64(&_hostCount, -1 * clearCount);
  return clearCount;
}

int ConnectionCount::getCount(const char *hostname, const int host_len)
{
  HostHashBucket *pBucket;
  HostEntry *found;
  int count;

  pBucket = _hostTable + time33Hash(hostname, host_len) % HOST_HASH_TABLE_SIZE;
  ink_mutex_acquire(&pBucket->_mutex);
  found = find(pBucket, hostname, host_len);
  count = found != NULL ? found->_count : 0;
  ink_mutex_release(&pBucket->_mutex);
  return count;
}

int ConnectionCount::incrementCount(const char *hostname, const int host_len, const int delta)
{
  HostHashBucket *pBucket;
  HostEntry *found;
  int count;
  int allocSize;

  pBucket = _hostTable + time33Hash(hostname, host_len) % HOST_HASH_TABLE_SIZE;
  ink_mutex_acquire(&pBucket->_mutex);
  found = find(pBucket, hostname, host_len);
  if (found != NULL) {
    found->_count += delta;
  }
  else {
    if (delta < 0) {
      ink_mutex_release(&pBucket->_mutex);
      return 0;
    }

    if (pBucket->_allocSize <= pBucket->_count) {
      if (pBucket->_allocSize == 0) {
        allocSize = 8;  //must be power of 2
      }
      else {
        allocSize = pBucket->_allocSize * 2;
      }

      if (allocSize <= MAX_HOST_COUNT_PER_BUCKET) {
        pBucket->_allocSize = allocSize;
        pBucket->_hosts = (HostEntry *)ats_realloc(pBucket->_hosts,
            sizeof(HostEntry) * pBucket->_allocSize);
      }
      else if (ink_get_hrtime() - pBucket->last_clear_time <= CLEAR_INTERVAL) {
        ink_mutex_release(&pBucket->_mutex);
        return INT_MAX;
      }
      else  {
        if (clear(pBucket) == 0) {
          ink_mutex_release(&pBucket->_mutex);
          return INT_MAX;
        }
      }
    }

    found = pBucket->_hosts + pBucket->_count;
    found->_count = delta;
    found->_hostname = (char *)ats_malloc(host_len + 1);
    memcpy(found->_hostname, hostname, host_len);
    *(found->_hostname + host_len) = '\0';
    found->_length = host_len;
    pBucket->_count++;
    if (pBucket->_count > 1) {
      qsort(pBucket->_hosts, pBucket->_count, sizeof(HostEntry),
          HostEntry::compare);
    }
    ink_atomic_increment64(&_hostCount, 1);
  }
  count = found->_count;
  ink_mutex_release(&pBucket->_mutex);

  return count;
}

void ConnectionCount::hostTableStat(int &hostCount, int &min, int &max, double &avg)
{
  HostHashBucket *pBucket;
  HostHashBucket *pEnd;
  int usedCount;

  hostCount = 0;
  usedCount = 0;
  min = -1;
  max = 0;
  pEnd = _hostTable + HOST_HASH_TABLE_SIZE;
  for (pBucket=_hostTable; pBucket<pEnd; pBucket++) {
    if (pBucket->_count > 0) {
      usedCount++;
      hostCount += pBucket->_count;

      if (pBucket->_count > max) {
        max = pBucket->_count;
      }
    }

    if (min < 0) {
      min = pBucket->_count;
    }
    else if (pBucket->_count < min) {
      min = pBucket->_count;
    }
  }
  if (usedCount > 0) {
    avg = (double)hostCount / (double)usedCount;
  }
  else {
    avg = 0.00;
  }
}

