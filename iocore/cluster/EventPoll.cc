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

#include "EventPoll.h"

EventPoll::EventPoll(const int size, int timeout) : _size(size)
{
  int bytes;

#if TS_USE_EPOLL
  _extra_events = EPOLLET;
  _timeout = timeout;
  _poll_fd = epoll_create(_size);
  bytes = sizeof(struct epoll_event) * size;
  _events = (struct epoll_event *)ats_malloc(bytes);
#elif TS_USE_KQUEUE
  _extra_events = INK_EV_EDGE_TRIGGER;
  _timeout.tv_sec = timeout / 1000;
  _timeout.tv_nsec = 1000000 * (timeout % 1000);
  _poll_fd = kqueue();
  bytes = sizeof(struct kevent) * size;
  _events = (struct kevent *)ats_malloc(bytes);
#elif TS_USE_PORT
  _extra_events = 0;
  _timeout.tv_sec = timeout / 1000;
  _timeout.tv_nsec = 1000000 * (timeout % 1000);
  _poll_fd = port_create();
  bytes = sizeof(port_event_t) * size;
  _events = (port_event_t *)ats_malloc(bytes);
#endif
}

EventPoll::~EventPoll()
{
  ats_free(_events);
  close(_poll_fd);
}

int EventPoll::attach(const int fd, const int e, void *data)
{
#if TS_USE_EPOLL
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.events = e | _extra_events;
  ev.data.ptr = data;
  return epoll_ctl(_poll_fd, EPOLL_CTL_ADD, fd, &ev);
#elif TS_USE_KQUEUE
  struct kevent ev[2];
  int n = 0;
  if (e & EVENTIO_READ) {
    EV_SET(&ev[n++], fd, EVFILT_READ, EV_ADD | _extra_events, 0, 0, data);
  }
  if (e & EVENTIO_WRITE) {
    EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ADD | _extra_events, 0, 0, data);
  }
  return kevent(_poll_fd, ev, n, NULL, 0, NULL);
#elif TS_USE_PORT
  return port_associate(_poll_fd, PORT_SOURCE_FD, fd, e, data);
#endif
}

int EventPoll::modify(const int fd, const int e, void *data)
{
#if TS_USE_EPOLL
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.events = e | _extra_events;
  ev.data.ptr = data;
  return epoll_ctl(_poll_fd, EPOLL_CTL_MOD, fd, &ev);
#elif TS_USE_KQUEUE
  struct kevent ev[2];
  int n = 0;
  if (e & EVENTIO_READ) {
    EV_SET(&ev[n++], fd, EVFILT_READ, EV_ADD | _extra_events, 0, 0, data);
  }
  else {
    EV_SET(&ev[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, data);
  }

  if (e & EVENTIO_WRITE) {
    EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ADD | _extra_events, 0, 0, data);
  }
  else {
    EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, data);
  }
  return kevent(_poll_fd, ev, n, NULL, 0, NULL);
#elif TS_USE_PORT
  return port_associate(_poll_fd, PORT_SOURCE_FD, fd, e, data);
#endif
}

int EventPoll::detach(const int fd)
{
#if TS_USE_EPOLL
  return epoll_ctl(_poll_fd, EPOLL_CTL_DEL, fd, NULL);
#elif TS_USE_PORT
  return port_dissociate(_poll_fd, PORT_SOURCE_FD, fd);
#else
  return 0;
#endif
}

int EventPoll::poll()
{
#if TS_USE_EPOLL
  return epoll_wait(_poll_fd, _events, _size, _timeout);
#elif TS_USE_KQUEUE
  return kevent(_poll_fd, NULL, 0, _events, _size, &_timeout);
#elif TS_USE_PORT
  int result;
  int retval;
  unsigned nget = 1;
  if((retval = port_getn(_poll_fd, _events,
          _size, &nget, &_timeout)) == 0)
  {
    result = (int)nget;
  } else {
    switch(errno) {
      case EINTR:
      case EAGAIN:
      case ETIME:
        if (nget > 0) {
          result = (int)nget;
        }
        else {
          result = 0;
        }
        break;
      default:
        result = -1;
        break;
    }
  }
  return result;
#else
#error port me
#endif
}

