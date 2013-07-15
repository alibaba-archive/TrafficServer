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

#define IP_HASH_TABLE_SIZE   10949

/**
 * Singleton class to keep track of the number of connections per host
 */
class ConnectionCount
{
  struct ConnAddr {
    IpEndpoint _addr;
    int _count;

    static int compare(const void *p1, const void *p2) {
      return ats_ip_addr_cmp(&((ConnAddr *)p1)->_addr, &((ConnAddr *)p2)->_addr);
    }
  };
 
  struct IpHashBucket {
    ink_mutex _mutex; //use for locking this bucket
    ConnAddr *_addrs; //ip address items
    int _allocSize;   //alloc count
    int _count;       //real count
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
   * @param ip IP address of the host
   * @return Number of connections
   */
  int getCount(const IpEndpoint& addr);

  /**
   * Change (increment/decrement) the connection count
   * @param ip IP address of the host
   * @param delta Default is +1, can be set to negative to decrement
   */
  void incrementCount(const IpEndpoint& addr, const int delta = 1);

protected:
  inline ConnAddr *find(IpHashBucket *pBucket, const IpEndpoint& addr)
  {
    ConnAddr target;

    if (pBucket->_count == 0) {
      return NULL;
    }
    if (pBucket->_count == 1) {
      if (ats_ip_addr_eq(&pBucket->_addrs->_addr, &addr)) {
        return pBucket->_addrs;
      }
      else {
        return NULL;
      }
    }

    target._addr = addr;
    return (ConnAddr *)bsearch(&target, pBucket->_addrs, pBucket->_count,
        sizeof(ConnAddr), ConnAddr::compare);
  }

private:
  // Hide the constructor and copy constructor
  ConnectionCount();
  ConnectionCount(const ConnectionCount & x) { NOWARN_UNUSED(x); }

  static ConnectionCount _connectionCount;
  IpHashBucket *_ipTable;
};

#endif

