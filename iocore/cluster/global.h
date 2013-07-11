//global.h

#ifndef _GLOBAL_H
#define _GLOBAL_H

#include "common_define.h"
#include "types.h"
#include "machine.h"

#ifdef __cplusplus
extern "C" {
#endif

extern bool g_continue_flag;
extern int g_accept_threads;
extern int g_work_threads;
extern int g_connections_per_machine;   //must be an even number
extern int g_connect_timeout;
extern int g_network_timeout;
extern int g_server_port;
extern int g_thread_stack_size;
extern int g_socket_recv_bufsize;
extern int g_socket_send_bufsize;
extern char g_base_path[MAX_PATH_SIZE];

#ifdef __cplusplus
}
#endif

#endif

