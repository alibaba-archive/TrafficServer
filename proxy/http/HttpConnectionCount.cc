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

#include "HttpConnectionCount.h"


ConnectionCount ConnectionCount::_connectionCount;

ConnectionCount::ConnectionCount() : _ipCount(0)
{
  int bytes;
  IpHashBucket *pBucket;
  IpHashBucket *pEnd;

  bytes = sizeof(IpHashBucket) * IP_HASH_TABLE_SIZE;
  _ipTable = (IpHashBucket *)ats_malloc(bytes);
  memset(_ipTable, 0, bytes);

  pEnd = _ipTable + IP_HASH_TABLE_SIZE;
  for (pBucket=_ipTable; pBucket<pEnd; pBucket++) {
    ink_mutex_init(&pBucket->_mutex, "ConnectionCountMutex");
  }
}

int ConnectionCount::getCount(const IpEndpoint& addr)
{
  IpHashBucket *pBucket;
  ConnAddr *found;
  int count;

  pBucket = _ipTable + ats_ip_hash(&addr.sa) % IP_HASH_TABLE_SIZE;
  ink_mutex_acquire(&pBucket->_mutex);
  found = find(pBucket, addr);
  count = found != NULL ? found->_count : 0;
  ink_mutex_release(&pBucket->_mutex);
  return count;
}

void ConnectionCount::incrementCount(const IpEndpoint& addr, const int delta)
{
  IpHashBucket *pBucket;
  ConnAddr *found;

  pBucket = _ipTable + ats_ip_hash(&addr.sa) % IP_HASH_TABLE_SIZE;
  ink_mutex_acquire(&pBucket->_mutex);
  found = find(pBucket, addr);
  if (found != NULL) {
    found->_count += delta;
  }
  else {
    if (pBucket->_allocSize <= pBucket->_count) {
      if (pBucket->_allocSize == 0) {
        pBucket->_allocSize = 8;
      }
      else {
        pBucket->_allocSize *= 2;
      }
      pBucket->_addrs = (ConnAddr *)ats_realloc(pBucket->_addrs,
          sizeof(ConnAddr) * pBucket->_allocSize);
    }

    found = pBucket->_addrs + pBucket->_count;
    found->_count = delta;
    found->_addr = addr;
    pBucket->_count++;
    if (pBucket->_count > 1) {
      qsort(pBucket->_addrs, pBucket->_count, sizeof(ConnAddr),
          ConnAddr::compare);
    }
    ink_atomic_increment64(&_ipCount, 1);
  }
  ink_mutex_release(&pBucket->_mutex);
}

