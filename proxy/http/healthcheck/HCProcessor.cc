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

#include "HCProcessor.h"
#include "HCHandler.h"

HCProcessor hcProcessor;

int
HCProcessor::start(int n_healthcheck_threads)
{
  start_read_config_values();

  if (0 == healthcheck_enabled) {
    Debug("healthcheck", "disable healthcheck");
    return EXIT_SUCCESS;
  }
  Debug("healthcheck", "enable healthcheck");

  char file_path[PATH_NAME_MAX+1];
  char dir_path[PATH_NAME_MAX+1];
  if (NULL == healthcheck_filename) {
    Error("no healthcheck file");
    return EXIT_FAILURE;
  }
  ink_strncpy(dir_path, Layout::get()->sysconfdir, sizeof(dir_path));
  if (0 != ink_filepath_make(file_path, sizeof(file_path), dir_path, healthcheck_filename)) {
    Error("invalid path: dir %s, file %s", dir_path, healthcheck_filename);
    return EXIT_FAILURE;
  }
  Debug("healthcheck", "file path: %s", file_path);
  int fd = open(file_path, O_RDONLY);
  if (-1 == fd) {
    Error("can't open file %s in read mode", file_path);
    return EXIT_FAILURE;
  }
  if (false == read_entry(fd)) {
    Error("syntax error in %s\n", file_path);
    return EXIT_FAILURE;
  }
  Vec<HCEntry *> entry_vec;
  entry_map.get_values(entry_vec);
  int entry_vec_len = entry_vec.length();
  for (int i = 0; i < entry_vec_len; ++i) {
    HCEntry *hc_entry = entry_vec[i];
    HCHandler *hc_handler = HCHandler::allocate();
    hc_handler->init(hc_entry);
    eventProcessor.schedule_every(hc_handler, HRTIME_SECONDS(healthcheck_default_ttl), ET_TASK);
  }

  return EXIT_SUCCESS;
}

