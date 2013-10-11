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

