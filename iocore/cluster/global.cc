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
int g_server_port = 8086;
int g_thread_stack_size = 1 * 1024 * 1024;
int g_socket_recv_bufsize = 1 * 1024 * 1024;
int g_socket_send_bufsize = 1 * 1024 * 1024;

//cluster flow control
int64_t cluster_flow_ctrl_min_bps = 0; //byte
int64_t cluster_flow_ctrl_max_bps = 0; //byte
int cluster_send_min_wait_time = 1000; //us
int cluster_send_max_wait_time = 5000; //us
int cluster_min_loop_interval = 0;     //us
int cluster_max_loop_interval = 1000;  //us
int64_t cluster_ping_send_interval= 0;
int64_t cluster_ping_latency_threshold = 0;
int cluster_ping_retries = 3;

volatile int64_t cluster_current_time = 0;

