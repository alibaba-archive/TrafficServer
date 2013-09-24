#include "global.h"

int cluster_connect_timeout = 1;

//cluster flow control
int64_t cluster_flow_ctrl_min_bps = 0; //bit
int64_t cluster_flow_ctrl_max_bps = 0; //bit
int cluster_send_min_wait_time = 1000; //us
int cluster_send_max_wait_time = 5000; //us
int cluster_min_loop_interval = 0;     //us
int cluster_max_loop_interval = 1000;  //us
int64_t cluster_ping_send_interval= 0;
int64_t cluster_ping_latency_threshold = 0;
int cluster_ping_retries = 3;
int max_session_count_per_machine = 1000000;
int session_lock_count_per_machine =  10949;

