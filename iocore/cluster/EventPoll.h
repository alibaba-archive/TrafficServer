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

#ifndef __EVENT_POLL_H__
#define __EVENT_POLL_H__

#include "P_Net.h"

class EventPoll {
  public:
    EventPoll(const int size, int timeout);
    ~EventPoll();
    int attach(const int fd, const int e, void *data);
    int modify(const int fd, const int e, void *data);
    int detach(const int fd);
    int poll();

#if TS_USE_KQUEUE
    /* we define these here as numbers, because for kqueue mapping them to a combination of
     * filters / flags is hard to do. */
    inline int kq_event_convert(int16_t event, uint16_t flags)
    {
      int r = 0;

      if (event == EVFILT_READ) {
        r |= INK_EVP_IN;
      }
      else if (event == EVFILT_WRITE) {
        r |= INK_EVP_OUT;
      }

      if (flags & EV_EOF) {
        r |= INK_EVP_HUP;
      }
      return r;
    }
#endif

    inline int getEvents(const int index)
    {
#if TS_USE_EPOLL
      return _events[index].events;
#elif TS_USE_KQUEUE
      /* we define these here as numbers, because for kqueue mapping them to a combination of
       * filters / flags is hard to do. */
      return kq_event_convert(_events[index].filter, _events[index].flags);
#elif TS_USE_PORT
      return _events[index].portev_events;
#else
#error port me
#endif
    }

    inline void *getData(const int index)
    {
#if TS_USE_EPOLL
      return _events[index].data.ptr;
#elif TS_USE_KQUEUE
      return _events[index].udata;
#elif TS_USE_PORT
      return _events[index].portev_user;
#else
#error port me
#endif
    }

  protected:
    int _size;  //max events (fd)
    int _extra_events;
    int _poll_fd;

#if TS_USE_EPOLL
    struct epoll_event *_events;
    int _timeout;
#elif TS_USE_KQUEUE
    struct kevent *_events;
    struct timespec _timeout;
#elif TS_USE_PORT
    port_event_t *_events;
    timespec_t _timeout;
#endif
};

#endif

