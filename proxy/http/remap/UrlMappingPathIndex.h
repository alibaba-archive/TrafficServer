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
#ifndef _URL_MAPPING_PATH_INDEX_H
#define _URL_MAPPING_PATH_INDEX_H

#include "libts.h"
#include "URL.h"
#include "UrlMapping.h"
#include "Trie.h"

class UrlMappingPathIndex
{
public:
  UrlMappingPathIndex() { }

  virtual ~UrlMappingPathIndex();

  bool Insert(url_mapping *mapping);

  inline url_mapping* Search(URL *request_url) const {
    int path_len;
    const char *path;

    path = request_url->path_get(&path_len);
    return _trie.Search(path, path_len);
  }

  void Print();

private:
  Trie<url_mapping> _trie;

  // make copy-constructor and assignment operator private
  // till we properly implement them
  UrlMappingPathIndex(const UrlMappingPathIndex &/*rhs*/) { };
  UrlMappingPathIndex &operator =(const UrlMappingPathIndex & /*rhs*/) { return *this; }
};

#endif // _URL_MAPPING_PATH_INDEX_H
