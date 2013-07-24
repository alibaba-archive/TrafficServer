#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ink_config.h"
#include "P_Cluster.h"
#include "global.h"

bool g_continue_flag = true;
int g_accept_threads = 1;
int g_work_threads = 2;
int g_connections_per_machine = 2;
int g_connect_timeout = 1;
int g_network_timeout = 5;
int g_server_port = 8086;
int g_thread_stack_size = 1 * 1024 * 1024;
int g_socket_recv_bufsize = 1 * 1024 * 1024;
int g_socket_send_bufsize = 1 * 1024 * 1024;

