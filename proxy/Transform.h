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

#ifndef __TRANSFORM_H__
#define __TRANSFORM_H__

#include "P_EventSystem.h"

#include "HTTP.h"
#include "InkAPIInternal.h"


#define TRANSFORM_READ_READY   (TRANSFORM_EVENTS_START + 0)

typedef struct _RangeRecord
{
  _RangeRecord() :
  _start(-1), _end(-1), _done_byte(-1)
  { }

  int64_t _start;
  int64_t _end;
  int64_t _done_byte;
} RangeRecord;

class RangeTransform:public INKVConnInternal
{
public:
  RangeTransform(ProxyMutex * mutex, MIMEField * range_field, HTTPInfo * cache_obj, HTTPHdr * transform_resp);
  ~RangeTransform();

  void parse_range_and_compare();
  int handle_event(int event, void *edata);

  void transform_to_range();
  void add_boundary(bool end);
  void add_sub_header(int index);
  void change_response_header();
  void calculate_output_cl();
  bool is_this_range_not_handled()
  {
    return m_not_handle_range;
  }
  bool is_range_unsatisfiable()
  {
    return m_unsatisfiable_range;
  }

  typedef struct _RangeRecord
  {
  _RangeRecord() :
    _start(-1), _end(-1), _done_byte(-1)
    { }

    int64_t _start;
    int64_t _end;
    int64_t _done_byte;
  } RangeRecord;

public:
  MIOBuffer * m_output_buf;
  IOBufferReader *m_output_reader;
  MIMEField *m_range_field;
  HTTPHdr *m_transform_resp;
  VIO *m_output_vio;
  bool m_unsatisfiable_range;
  bool m_not_handle_range;
  int64_t m_content_length;
  int m_num_chars_for_cl;
  int m_num_range_fields;
  int m_current_range;
  const char *m_content_type;
  int m_content_type_len;
  RangeRecord *m_ranges;
  int64_t m_output_cl;
  int64_t m_done;
};

class TransformProcessor
{
public:
  void start();

public:
  VConnection * open(Continuation * cont, APIHook * hooks);
  INKVConnInternal *null_transform(ProxyMutex * mutex);
  RangeTransform *range_transform(ProxyMutex * mutex, MIMEField * range_field, HTTPInfo * cache_obj,
                                    HTTPHdr * transform_resp, bool & b);
};


#ifdef TS_HAS_TESTS
class TransformTest
{
public:
  static void run();
};
#endif


extern TransformProcessor transformProcessor;


#endif /* __TRANSFORM_H__ */

