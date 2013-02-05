#ifndef  _REMAP_PARSER_H
#define  _REMAP_PARSER_H

#include "RemapDirective.h"

class RemapDirective;
class DirectiveParams;

class RemapParser {
  public:
    RemapParser();
    ~RemapParser();
    int loadFromFile(const char *filename, DirectiveParams *rootParams);
  private:
    RemapDirective *_rootDirective;
    char *_content;  //the content of config file

    void init();
    int getFileContent(const char *filename, char *&content, int *fileSize);

    inline void getLine(char *str, char *strEnd, char *&lineEnd) {
      lineEnd = (char *)memchr(str, '\n', strEnd - str);
      if (lineEnd != NULL) {
        lineEnd++; //skip \n
      }
      else {
        lineEnd = strEnd;
      }
    }

    inline void trimLeft(char *&str, const char *strEnd) {
      while ((str < strEnd) && (*str == ' ' || *str == '\t' 
            || *str == '\r' || *str == '\n'))
      {
        str++;
      }
    }

    inline void trimRight(const char *str, char *&strEnd) {
      char ch;
      while (strEnd > str) {
        ch = *(strEnd - 1);
        if (!(ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')) {
          break;
        }

        strEnd--;
      }
    }

    inline void trim(char *&str, char *&strEnd) {
      trimLeft(str, strEnd);
      trimRight(str, strEnd);
    }

    inline void getToken(const char *str, const char *strEnd, int *tokenLen) {
      const char *p = str;
      while ((p < strEnd) && !(*p == ' ' || *p == '\t' 
            || *p == '\r' || *p == '\n'))
      {
        p++;
      }

      *tokenLen = p - str;
    }

    int parse(DirectiveParams *params, char *content, char *contentEnd);
};

#endif

