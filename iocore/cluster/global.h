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

//cluster flow control
extern int64_t cluster_flow_ctrl_min_bps; //byte
extern int64_t cluster_flow_ctrl_max_bps; //byte
extern int cluster_send_min_wait_time; //us
extern int cluster_send_max_wait_time; //us
extern int cluster_min_loop_interval;  //us
extern int cluster_max_loop_interval;  //us
extern int64_t cluster_ping_send_interval;
extern int64_t cluster_ping_latency_threshold;
extern int cluster_ping_retries;

#ifdef __cplusplus
}
#endif

#endif

