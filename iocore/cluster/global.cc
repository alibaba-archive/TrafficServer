#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"

#ifndef DEBUG_FLAG
#include "ink_config.h"
#include "P_Cluster.h"
#endif

bool g_continue_flag = true;
int g_accept_threads = 1;
int g_work_threads = 2;
int g_connections_per_machine = 2;
int g_connect_timeout = 1;
int g_network_timeout = 5;

#ifndef DEBUG_FLAG
int g_server_port = 8086;
#else
int g_server_port = 10088;
#endif

int g_thread_stack_size = 1 * 1024 * 1024;
int g_socket_recv_bufsize = 1 * 1024 * 1024;
int g_socket_send_bufsize = 1 * 1024 * 1024;
char g_base_path[MAX_PATH_SIZE] = {'/', 't', 'm', 'p', '\0'};

