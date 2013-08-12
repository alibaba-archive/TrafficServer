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
#include "libts.h"
#include "Map.h"

#ifndef _HTTP_CONNECTION_COUNT_H_

#define IP_HASH_TABLE_SIZE  1361

/**
 * Singleton class to keep track of the number of connections per host
 */
class ConnectionCount
{
  struct HostEntry {
    char *_hostname;
    int _length;
    int _count;
    
    inline bool equals(const char *hostname, const int host_len) {
      return (_length == host_len && memcmp(_hostname, hostname, host_len) == 0);
    }

    inline static int compare(const void *p1, const void *p2)
    {
      HostEntry *host1;
      HostEntry *host2;
      host1 = (HostEntry *)p1;
      host2 = (HostEntry *)p2;
      if (host1->_length < host2->_length) {
        return -1;
      }
      else if (host1->_length > host2->_length) {
        return 1;
      }
      else {
        return memcmp(host1->_hostname, host2->_hostname, host1->_length);
      }
    }
  };
 
  struct HostHashBucket {
    ink_mutex _mutex;  //use for locking this bucket
    HostEntry *_hosts; //hosts items
    int _allocSize;    //alloc count
    int _count;        //real count
  };

public:
  /**
   * Static method to get the instance of the class
   * @return Returns a pointer to the instance of the class
   */
  inline static ConnectionCount *getInstance() {
    return &_connectionCount;
  }

  /**
   * Gets the number of connections for the host
   * @param hostname the host
   * @param host_len the hostname length
   * @return Number of connections
   */
  int getCount(const char *hostname, const int host_len);

  /**
   * Change (increment/decrement) the connection count
   * @param hostname the host
   * @param host_len the hostname length
   * @param delta can be set to negative to decrement
   * @return connection count after increment
   */
  int incrementCount(const char *hostname, const int host_len, const int delta);

  /**
   * return total count of host
   */
  inline int64_t getHostCount() {
    return _hostCount;
  }

  /**
   * stat host hash table
   * @param hostCount the host count
   * @param min the min host count of a bucket
   * @param max the max host count of a bucket
   * @param avg the avg host count
   */
  void hostTableStat(int &hostCount, int &min, int &max, double &avg);

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

  inline HostEntry *find(HostHashBucket *pBucket, const char *hostname, const int host_len)
  {
    HostEntry target;

    if (pBucket->_count == 0) {
      return NULL;
    }
    if (pBucket->_count == 1) {
      if (pBucket->_hosts->equals(hostname, host_len)) {
        return pBucket->_hosts;
      }
      else {
        return NULL;
      }
    }

    target._hostname = (char *)hostname;
    target._length = host_len;
    return (HostEntry *)bsearch(&target, pBucket->_hosts, pBucket->_count,
        sizeof(HostEntry), HostEntry::compare);
  }

private:
  // Hide the constructor and copy constructor
  ConnectionCount();
  ConnectionCount(const ConnectionCount & x) { NOWARN_UNUSED(x); }

  static ConnectionCount _connectionCount;
  volatile int64_t _hostCount;
  HostHashBucket *_hostTable;
};

#endif

