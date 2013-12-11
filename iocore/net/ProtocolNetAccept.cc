/** @file

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

#include "ink_config.h"
#include "P_Net.h"
#include "P_ProtocolNetAccept.h"

UnixNetVConnection *
ProtocolNetAccept::createSuitableVC(EThread *t, Connection &con)
{
  UnixNetVConnection *vc;

  if (etype == SSLNetProcessor::ET_SSL && etype) {
    // SSL protocol
    if (t)
      vc = (UnixNetVConnection *)THREAD_ALLOC(sslNetVCAllocator, t);
    else
      vc = (UnixNetVConnection *)sslNetVCAllocator.alloc();
    vc->proto_type = NET_PROTO_HTTP_SSL;
  } else {
    //
    // To detect SPDY or HTTP protocol by
    // reading the first byte.
    //
    int n;
    unsigned char c;

    do {
      n = recv(con.fd, &c, 1, MSG_PEEK);
    } while(n < 0 && (errno == EAGAIN || errno == EINTR));

    //
    // Connection shutdown or other errors.
    //
    if (n <= 0) {
      char str[INET6_ADDRSTRLEN];
      ats_ip_nptop(&con.addr, str, INET6_ADDRSTRLEN);
      return NULL;
    }

    Debug("spdy", "the first byte:%x", c);
    if (c == 0x80 || c == 0x00) {
      // SPDY protocol
      if (t)
        vc = THREAD_ALLOC(netVCAllocator, t);
      else
        vc = netVCAllocator.alloc();
      vc->proto_type = NET_PROTO_HTTP_SPDY;
    } else {
      // HTTP protocol
      if (t)
        vc = THREAD_ALLOC(netVCAllocator, t);
      else
        vc = netVCAllocator.alloc();
      vc->proto_type = NET_PROTO_HTTP;
    }
  }

  vc->con = con;
  return vc;
}

NetAccept *
ProtocolNetAccept::clone()
{
  NetAccept *na;
  na = NEW(new ProtocolNetAccept);
  *na = *this;
  return na;
}
