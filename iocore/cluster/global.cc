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

