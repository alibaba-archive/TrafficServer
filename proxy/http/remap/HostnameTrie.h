#ifndef _HOSTNAME_TRIE_H
#define _HOSTNAME_TRIE_H

#include "MappingTypes.h"

#define DOT_INDEX  1
#define VALID_CHAR_NUM   39

template<typename T>
class HostnameTrie
{
  public:
    HostnameTrie() : _matchDomain(true) {
      memset(&_root, 0, sizeof(_root));
    }

    HostnameTrie(const bool matchDomain) : _matchDomain(matchDomain) {
      memset(&_root, 0, sizeof(_root));
    }

    virtual ~HostnameTrie() {
      for (int i=0; i<VALID_CHAR_NUM; i++) {
        if (_root.children[i] != NULL) {
          freeTrieNode(_root.children[i]);
          _root.children[i] = NULL;
        }
      }
    }

    bool insert(const char *hostname, const int hostname_len, T *value);

    struct TrieNode {
      T *value;
      struct TrieNode *parent;
      struct TrieNode *children[VALID_CHAR_NUM];
      char c;
    };

    struct LookupState {
      const unsigned char *p;
      TrieNode * current_node;
    };

    T *lookupFirst(const char *hostname, const int hostname_len,
        LookupState * state);
    T *lookupNext(const char *hostname, const int hostname_len,
        LookupState * state);
    void print();

    inline T *lookup(const char *hostname, const int hostname_len)
    {
      LookupState state;
      return this->lookupFirst(hostname, hostname_len, &state);
    }

    T *lookupLast(const char *hostname, const int hostname_len)
    {
      LookupState state;
      T *value;
      T *lastValue;
      value = this->lookupFirst(hostname, hostname_len, &state);
      if (value == NULL) {
        return NULL;
      }

      lastValue = value;
      while ((value=this->lookupNext(hostname, hostname_len,
              &state)) != NULL)
      {
        lastValue = value;
      }

      return lastValue;
    }

    bool empty() const {
      for (int i=0; i<VALID_CHAR_NUM; i++) {
        if (_root.children[i] != NULL) {
          return true;
        }
      }

      return false;
    }

    T **getNodes(int *count) {
      if (_nodes.count > 0) {
        _nodes.count = 0;
      }

      getNodes(&_root);
      *count = _nodes.count;
      return _nodes.items;
    }

    void getNodes(TrieNode * node)
    {
      if (node->value != NULL) {
        _nodes.add(node->value);
      }

      for (int i=0; i<VALID_CHAR_NUM; i++) {
        if (node->children[i] != NULL) {
          getNodes(node->children[i]);
        }
      }
    }

  protected:
    int allocTrieNode(TrieNode ** node);
    void freeTrieNode(TrieNode * node);
    void print(TrieNode * node);

    TrieNode _root;
    bool _matchDomain;  //if match domain, taobao.com matchs taobao.com and *.taobao.com
    DynamicArray<T *> _nodes;  //for getNodes
};

class HostnameTrieSet : public HostnameTrie<int>
{
  public:
    HostnameTrieSet() : HostnameTrie<int>()
    {
    }

    ~HostnameTrieSet() {
      if (_hosts.count > 0) {
        for (int i=0; i<_hosts.count; i++) {
          free(_hosts.items[i]);
          _hosts.items[i] = NULL;
        }
      }
    }

    //for trie set, value is a flag only
    inline bool insert(const char *hostname, const int hostname_len)
    {
      return HostnameTrie<int>::insert(hostname, hostname_len, (int *)1);
    }

    //for trie set, value is a flag only
    inline bool insert(const char *hostname)
    {
      return this->insert(hostname, strlen(hostname));
    }

    //trie set lookup
    inline bool contains(const char *hostname, const int hostname_len)
    {
      return this->lookup(hostname, hostname_len) != NULL;
    }

    char **getHostnames(int *count) {
      if (_hosts.count > 0) {
        _hosts.count = 0;
      }

      getHostnames(&_root);
      *count = _hosts.count;
      return _hosts.items;
    }

  protected:
    DynamicArray<char *> _hosts;

    void getHostnames(TrieNode * node)
    {
      int i;
      if (node->value != NULL) {
        char buff[256];
        TrieNode * temp;
        char *p;

        p = buff;
        temp = node;
        while (temp != NULL) {
          *p++ = temp->c;
          temp = temp->parent;
        }
        *p = '\0';

        _hosts.add(strdup(buff));
      }

      for (i=0; i<VALID_CHAR_NUM; i++) {
        if (node->children[i] != NULL) {
          getHostnames(node->children[i]);
        }
      }
    }
};

static const signed char _ascii2table[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1,   // 0 - 7
    -1, -1, -1, -1, -1, -1, -1, -1,   // 8 - 15
    -1, -1, -1, -1, -1, -1, -1, -1,   // 16 - 23
    -1, -1, -1, -1, -1, -1, -1, -1,   // 24 - 31
    -1, -1, -1, -1, -1, -1, -1, -1,   // 32 - 39
    -1, -1, -1, -1, -1,  0,  1, -1,   // 40 - 47 ('-', '.')
     2,  3,  4,  5,  6,  7,  8,  9,   // 48 - 55 (0-7)
    10, 11, -1, -1, -1, -1, -1, -1,   // 56 - 63 (8-9)
    -1, 12, 13, 14, 15, 16, 17, 18,   // 64 - 71 (A-G)
    19, 20, 21, 22, 23, 24, 25, 26,   // 72 - 79 (H-O)
    27, 28, 29, 30, 31, 32, 33, 34,   // 80 - 87 (P-W)
    35, 36, 37, -1, -1, -1, -1, 38,   // 88 - 95 (X-Z, '_')
    -1, 12, 13, 14, 15, 16, 17, 18,   // 96 - 103 (a-g)
    19, 20, 21, 22, 23, 24, 25, 26,   // 104 - 111 (h-0)
    27, 28, 29, 30, 31, 32, 33, 34,   // 112 - 119 (p-w)
    35, 36, 37, -1, -1, -1, -1, -1,   // 120 - 127 (x-z)
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1
};

template<typename T>
int HostnameTrie<T>::allocTrieNode(TrieNode ** node)
{
    int result;

    *node = (TrieNode *)malloc(sizeof(TrieNode));
    if (*node == NULL) {
        result = errno != 0 ? errno : ENOMEM;
        fprintf(stderr, "file: "__FILE__", line: %d, " \
                "malloc %d bytes fail, errno: %d, error info: %s\n",
                __LINE__, (int)sizeof(TrieNode), result, strerror(result));
        return result;
    }

    memset(*node, 0, sizeof(TrieNode));
    return 0;
}

template<typename T>
void HostnameTrie<T>::freeTrieNode(TrieNode * node)
{
    int i;

    for (i=0; i<VALID_CHAR_NUM; i++) {
        if (node->children[i] != NULL) {
            freeTrieNode(node->children[i]);
        }
    }

    free(node);
}

template<typename T>
bool HostnameTrie<T>::insert(const char *hostname, const int hostname_len, T *value)
{
    const unsigned char *p;
    TrieNode *current_node;
    int result;
    int index;

    if (hostname_len <= 0) {
      return false;
    }

    current_node = &_root;
    p = (const unsigned char *)hostname + hostname_len - 1;
    while (p >= (const unsigned char *)hostname) {
        index = _ascii2table[*p];
        if (index < 0) {
            fprintf(stderr, "file: "__FILE__", line: %d, " \
                    "invalid hostname: %s\n", __LINE__, hostname);
            return false;
        }

        if (current_node->children[index] == NULL) {
            result = allocTrieNode((&current_node->children[index]));
            if (result != 0) {
                return false;
            }

            current_node->children[index]->c = *p;
            current_node->children[index]->parent = current_node;
        }

        current_node = current_node->children[index];
        --p;
    }

    if (current_node->value == NULL) {
      current_node->value = value;
    }
    else {
      fprintf(stderr, "file: "__FILE__", line: %d, " \
          "Can not insert duplicate: %.*s!\n", __LINE__, hostname_len, hostname);
      return false;
    }

    return true;
}

template<typename T>
void HostnameTrie<T>::print(TrieNode * node)
{
    int i;
    if (node->value != NULL) {
        char buff[128];
        TrieNode * temp;
        char *p;

        p = buff;
        temp = node;
        while (temp != NULL) {
            *p++ = temp->c;
            temp = temp->parent;
        }
        *p = '\0';
        printf("%s\n", buff);
    }

    for (i=0; i<VALID_CHAR_NUM; i++) {
        if (node->children[i] != NULL) {
            print(node->children[i]);
        }
    }
}

template<typename T>
void HostnameTrie<T>::print()
{
    print(&_root);
    printf("\n");
}

template<typename T>
T *HostnameTrie<T>::lookupFirst(const char *hostname, const int hostname_len,
    HostnameTrie::LookupState * state)
{
    if (hostname == NULL || hostname_len == 0) {
        return NULL;
    }

    state->p = (const unsigned char *)hostname + hostname_len - 1;
    state->current_node = &_root;
    return this->lookupNext(hostname, hostname_len, state);
}

template<typename T>
T *HostnameTrie<T>::lookupNext(const char *hostname, const int hostname_len,
    HostnameTrie::LookupState * state)
{
    int index;

    if (hostname == NULL || hostname_len == 0 ||
            state->p < (const unsigned char *)hostname) {
        return NULL;
    }

    while (state->p >= (const unsigned char *)hostname) {
        index = _ascii2table[*(state->p)];
        if (index < 0) {
            return NULL;
        }

        if (state->current_node->children[index] == NULL) {
          return NULL;
        }

        state->current_node = state->current_node->children[index];
        if (state->current_node->value != NULL && *(state->p) == '.') {
            state->p--;
            return state->current_node->value;
        }

        state->p--;
    }

    if (state->current_node->value != NULL) {
      return state->current_node->value;
    }

    if (!_matchDomain) {
      return NULL;
    }

    TrieNode * current_node = state->current_node->children[DOT_INDEX];
    if (current_node == NULL) {
      return NULL;
    }

    if (current_node->value != NULL) {
      return current_node->value;
    }

    return NULL;
}

#endif

