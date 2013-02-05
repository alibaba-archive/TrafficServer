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
#include "UrlMappingPathIndex.h"

UrlMappingPathIndex::~UrlMappingPathIndex()
{
}

bool
UrlMappingPathIndex::Insert(url_mapping *mapping)
{
  int from_path_len;
  const char *from_path;

  from_path = mapping->fromURL.path_get(&from_path_len);
  if (!_trie.Insert(from_path, mapping, mapping->getRank(), from_path_len)) {
    Error("Couldn't insert into trie!");
    return false;
  }
  Debug("UrlMappingPathIndex::Insert", "Inserted new element!");
  return true;
}

void
UrlMappingPathIndex::Print()
{
  _trie.Print();
}

