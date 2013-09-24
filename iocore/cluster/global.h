//global.h

#ifndef _GLOBAL_H
#define _GLOBAL_H

#include <stdint.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int num_of_cluster_threads;
extern int num_of_cluster_connections;   //must be an even number
extern int cluster_send_buffer_size;
extern int cluster_receive_buffer_size;
extern int cluster_connect_timeout;  //second

//cluster flow control
extern int64_t cluster_flow_ctrl_min_bps; //bit
extern int64_t cluster_flow_ctrl_max_bps; //bit
extern int cluster_send_min_wait_time; //us
extern int cluster_send_max_wait_time; //us
extern int cluster_min_loop_interval;  //us
extern int cluster_max_loop_interval;  //us

//cluster ping
extern int64_t cluster_ping_send_interval;
extern int64_t cluster_ping_latency_threshold;
extern int cluster_ping_retries;

extern int max_session_count_per_machine;
extern int session_lock_count_per_machine;

#ifdef __cplusplus
}
#endif

#endif

