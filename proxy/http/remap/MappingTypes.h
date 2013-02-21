#ifndef _MAPPING_TYPES_H
#define _MAPPING_TYPES_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#define CONFIG_TYPE_NONE              0
#define CONFIG_TYPE_RECORDS_INT       1
#define CONFIG_TYPE_HOSTING_INT       2
#define CONFIG_TYPE_CACHE_CONTROL_INT 3
#define CONFIG_TYPE_CONGESTION_INT    4

#define MAPPING_FLAG_NONE        0
#define MAPPING_FLAG_REGEX       1
#define MAP_FLAG_WITH_RECV_PORT  2
#define MAP_FLAG_REVERSE         4
#define REDIRECT_FALG_TEMPORARY  8

#define MAPPING_TYPE_NONE       0
#define MAPPING_TYPE_MAP       'm'
#define MAPPING_TYPE_REDIRECT  'r'

#define ACL_ACTION_NONE_INT    0
#define ACL_ACTION_ALLOW_INT   1
#define ACL_ACTION_DENY_INT    2

#define MAX_PARAM_NUM 11

struct StringValue {
  const char *str;
  int length;

  StringValue() : str(NULL), length(0) {}

  StringValue(const char *theStr, const int theLength) :
    str(theStr), length(theLength)
  {
  }

  StringValue(const StringValue & src) :
    str(src.str), length(src.length)
  {
  }

  inline bool equals(const char *s, const int len) const {
    return (len == this->length && memcmp(s, this->str, len) == 0);
  }

  inline bool equals(const StringValue *sv) const {
    return (sv->length == this->length && memcmp(sv->str,
          this->str, sv->length) == 0);
  }

  inline bool equalsIgnoreCase(const char *s, const int len) const {
    return (len == this->length && strncasecmp(s, this->str, len) == 0);
  }

  inline bool equalsIgnoreCase(const StringValue *sv) const {
    return (sv->length == this->length && strncasecmp(sv->str,
          this->str, sv->length) == 0);
  }

  StringValue *strdup(StringValue *dest) const {
    char *newStr;
    dest->length = this->length;
    newStr = (char *)malloc(this->length + 1);
    if (newStr == NULL) {
      fprintf(stderr, "malloc %d bytes fail, error info: %s\n",
          this->length + 1, strerror(errno));
      return NULL;
    }

    memcpy(newStr, this->str, this->length);
    *(newStr + this->length) = '\0';
    dest->str = (const char *)newStr;
    return dest;
  }

  void free() {
    if (this->str != NULL) {
      ::free((void *)(this->str));
      this->str = NULL;
      this->length = 0;
    }
  }
};

struct StringIntPair {
  StringValue s;
  int i;
};

struct ConfigKeyValue {
  StringValue key;
  StringValue value;

  ConfigKeyValue *duplicate(ConfigKeyValue *dest) const {
    if (this->key.strdup(&dest->key) == NULL) {
      return NULL;
    }

    if (this->value.strdup(&dest->value) == NULL) {
      return NULL;
    }

    return dest;
  }

  void free() {
    key.free();
    value.free();
  }

};

struct PluginInfo {
  StringValue filename;
  int paramCount;
  StringValue params[MAX_PARAM_NUM - 1];

  PluginInfo() : paramCount(0) {}

  PluginInfo *duplicate(PluginInfo *dest) const {
    if (this->filename.strdup(&dest->filename) == NULL) {
      return NULL;
    }

    dest->paramCount = this->paramCount;
    for (int i=0; i<this->paramCount; i++) {
      if (this->params[i].strdup(dest->params + i) == NULL) {
        return NULL;
      }
    }

    return dest;
  }

  void free() {
    this->filename.free();
    for (int i=0; i<this->paramCount; i++) {
      this->params[i].free();
    }
  }
};

struct ACLContext {
  StringValue method;
  StringValue refererUrl;
  StringValue refererHostname;
  unsigned long clientIp;
};

template<typename T>
struct DynamicArray {
  T *items;      //the items
  int count;     //the item count
  int allocSize; //alloced item count
  int initSize;  //init capacity

  DynamicArray(const int theInitSize) :
    items(NULL), count(0), allocSize(0), initSize(theInitSize)
  {
  }

  DynamicArray() : items(NULL), count(0), allocSize(0), initSize(0)
  {
  }

  DynamicArray(DynamicArray<T> &src) : items(src.items),
    count(src.count), allocSize(src.allocSize), initSize(src.initSize)
  {
  }

  ~DynamicArray() {
    if (items != NULL) {
      free(items);
      items = NULL;
      allocSize = 0;
      count = 0;
    }
  }

  void reset(const int theInitSize) {
    items = NULL;
    allocSize = 0;
    count = 0;
    initSize = theInitSize;
  }

  DynamicArray<T> *duplicate(DynamicArray<T> *dest) const {
    if (this->count == 0) {
      return dest;
    }

    dest->allocSize = this->count;

    int bytes = sizeof(T) * dest->allocSize;
    dest->items = (T *)malloc(bytes);
    if (dest->items == NULL) {
      fprintf(stderr, "malloc %d bytes fail, error info: %s",
          bytes, strerror(errno));
      return NULL;
    }

    dest->count = this->count;
    memcpy(dest->items, this->items, bytes);
    return dest;
  }

  bool checkSize() {
    if (allocSize > count) {
      return true;
    }

    if (allocSize == 0) {
      allocSize = (initSize > 0  ? initSize : 8);
    }
    else {
      allocSize *= 2;
    }

    int bytes = sizeof(T) * allocSize;
    T * newItems = (T *)malloc(bytes);
    if (newItems == NULL) {
      fprintf(stderr, "malloc %d bytes fail, error info: %s",
          bytes, strerror(errno));
      return false;
    }

    memset(newItems + count, 0, sizeof(T) * (allocSize - count));
    if (items != NULL) {
      memcpy(newItems, items, sizeof(T) * count);
      free(items);
    }

    items = newItems;
    return true;
  }

  bool add(T v) {
    if (!this->checkSize()) {
      return false;
    }

    items[count++] = v;
    return true;
  }
};

#endif

