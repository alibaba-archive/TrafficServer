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

#ifndef _CONNECTION_H_
#define _CONNECTION_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"

typedef struct socket_context_array {
  SocketContext **contexts;
  unsigned int alloc_size;   //alloc size
  unsigned int count;        //item count
  volatile unsigned int index;   //current select index
} SocketContextArray;

typedef struct socket_context_by_machine {
  unsigned int ip;
  socket_context_array connected_list;  //connected sockets
  SocketContext *accept_free_list;  //socket malloc for accept
  SocketContext *connect_free_list; //socket malloc for connect
} SocketContextsByMachine;

#ifdef __cplusplus
extern "C" {
#endif

int connection_init();
void connection_destroy();

int connection_manager_init(const unsigned int my_ip);
void connection_manager_destroy();
int connection_manager_start();

int log_message_stat(void *arg);

SocketContext *get_socket_context(const ClusterMachine *machine);

void free_accept_sock_context(SocketContext *pSockContext);

int machine_make_connections(ClusterMachine *m);
int machine_stop_reconnect(ClusterMachine *m);
int make_connection(SocketContext *pSockContext);

int add_machine_sock_context(SocketContext *pSockContext);
int remove_machine_sock_context(SocketContext *pSockContext);

#ifdef __cplusplus
}
#endif

#endif

