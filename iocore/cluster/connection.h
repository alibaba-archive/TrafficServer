//connection.h

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

