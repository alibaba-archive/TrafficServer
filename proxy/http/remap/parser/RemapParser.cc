#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/file.h>
#include <dirent.h>
#include "RemapParser.h"
#include "SchemeDirective.h"
#include "PluginDirective.h"
#include "ACLDirective.h"
#include "MappingDirective.h"

RemapParser::RemapParser() : _content(NULL)
{
  this->init();
}

RemapParser::~RemapParser()
{
  if (_content != NULL) {
    free(_content);
    _content = NULL;
  }

  if (_rootDirective != NULL) {
    delete _rootDirective;
    _rootDirective = NULL;
  }
}

void RemapParser::init()
{
  int index;
  _rootDirective = new RemapDirective("root", DIRECTIVE_TYPE_BLOCK, 0, 0);
  index = 0;
  _rootDirective->_children[index++] = new ACLDirective();
  _rootDirective->_children[index++] = new SchemeDirective(DIRECTVIE_NAME_HTTP);
  _rootDirective->_children[index++] = new SchemeDirective(DIRECTVIE_NAME_HTTPS);
  _rootDirective->_children[index++] = new SchemeDirective(DIRECTVIE_NAME_TUNNEL);
  _rootDirective->_children[index++] = new MapDirective();
  _rootDirective->_children[index++] = new RegexMapDirective();  //for backward compatibility
  _rootDirective->_children[index++] = new RedirectDirective();
  _rootDirective->_childrenCount = index;
}

int RemapParser::loadFromFile(const char *filename, DirectiveParams *rootParams)
{
  int result;
  int fileSize;

  if (_content != NULL) {
    free(_content);
  }
  result = this->getFileContent(filename, _content, &fileSize);
  if (result != 0) {
    return result;
  }

  rootParams->_directive = _rootDirective;
  return this->parse(rootParams, _content, _content + fileSize);
}

int RemapParser::parse(DirectiveParams *params, char *content, char *contentEnd)
{
  char *current;
  char *line;
  char *lineEnd;
  char *newLineEnd;
  char *blockStart;
  char *blockStatementStart;
  char *blockEnd;
  char *paramStr;
  char *paramEnd;
  RemapDirective *pChildDirective;
  DirectiveParams *pChildParams;
  int notMatchBlocks;
  int tokenLen;
  int lineNo;
  int lineCount;
  int result;
  char directiveName[MAX_DIRECTIVE_NAME_SIZE];
  bool bBlock;

  lineNo = 0;
  current = content;
  while (current < contentEnd) {
    line = current;
    getLine(current, contentEnd, lineEnd);
    lineNo++;

    newLineEnd = lineEnd;
    trim(line, newLineEnd);
    if (line == lineEnd || *line == '#') {
      current = lineEnd;
      continue;
    }

    lineCount = 0;
    blockStart = newLineEnd - 1;
    if (*blockStart == '{') {
      paramEnd = blockStart;
      blockStatementStart = lineEnd;
      bBlock = true;
    }
    else {
      paramEnd = newLineEnd;
      blockStart = lineEnd;
      while ((blockStart < contentEnd)) {
        if (*blockStart == ' ' || *blockStart == '\t' || *blockStart == '\r')
        {
          blockStart++;
        }
        else if (*blockStart == '\n')
        {
          blockStart++;
          lineCount++;
        }
        else {
          break;
        }
      }

      blockStatementStart = blockStart + 1;
      bBlock = (blockStart < contentEnd && *blockStart == '{');
    }

    if (bBlock) {
      notMatchBlocks = 1;
      blockEnd = blockStatementStart;
      while (blockEnd < contentEnd) {
        if (*blockEnd == '}') {
          notMatchBlocks--;
          if (notMatchBlocks == 0) { //matched
            blockEnd++;
            break;
          }
          else if (notMatchBlocks < 0) {
            fprintf(stderr, "file: "__FILE__", line: %d, "
                "unexpect }, config line: %.*s", __LINE__,
                (int)(lineEnd - line), line);
            return EINVAL;
          }
        }
        else if (*blockEnd == '{') {
          notMatchBlocks++;
        }
        else if (*blockEnd == '\n') {
          lineCount++;
        }

        blockEnd++; //skip }
      }

      if (notMatchBlocks != 0) {
        fprintf(stderr, "file: "__FILE__", line: %d, "
            "expect }, config line: %.*s", __LINE__,
            (int)(lineEnd - line), line);
        return EINVAL;
      }
    }
    else {
      blockStatementStart = NULL;
      blockEnd = NULL;
    }

    getToken(line, lineEnd, &tokenLen);
    if (tokenLen >= (int)sizeof(directiveName)) {
        fprintf(stderr, "file: "__FILE__", line: %d, "
            "ignore too long directive: %.*s, config line: %.*s", __LINE__,
            tokenLen, line, (int)(lineEnd - line), line);
        return EINVAL;
    }

    memcpy(directiveName, line, tokenLen);
    *(directiveName + tokenLen) = '\0';
    pChildDirective = params->_directive->getChild(directiveName);
    if (pChildDirective == NULL) {
      fprintf(stderr, "file: "__FILE__", line: %d, "
          "unkown directive: %s, config line: %.*s", __LINE__,
          directiveName, (int)(lineEnd - line), line);
      return EINVAL;
    }

    //fprintf(stderr, "%d %.*s\n", lineNo, tokenLen, line);

    //fprintf(stderr, "parent: %d, current: %d\n",
    //    params->_lineInfo.lineNo, lineNo);

    paramStr = line + tokenLen;
    while ((paramStr < paramEnd) && (*paramStr == ' ' || *paramStr == '\t')) {
      paramStr++;
    }
    pChildParams = pChildDirective->newDirectiveParams(
        params->_lineInfo.lineNo + lineNo, line,
        lineEnd - line, params, paramStr,
        paramEnd - paramStr, bBlock);
    if (pChildParams == NULL) {
      return EINVAL;
    }

    if ((result=pChildDirective->check(pChildParams, bBlock)) != 0) {
      delete pChildParams;
      return result;
    }

    if ((result=pChildParams->parse(blockStart, blockEnd)) != 0) {
      delete pChildParams;
      return result;
    }

    params->addChild(pChildParams);
    if (bBlock && pChildDirective->getChildrenCount() > 0) {
      if ((result=parse(pChildParams, blockStatementStart, blockEnd - 1)) != 0) {
        return result;
      }
    }

    lineNo += lineCount;

    //printf("%d %.*s", ++i, (int)(lineEnd - line), line);

    if (bBlock) {
      current = blockEnd;
    }
    else {
      current = lineEnd;
    }
  }

  return 0;
}

int RemapParser::getFileContent(const char *filename, char *&content, int *fileSize)
{
  int fd;

  fd = open(filename, O_RDONLY);
  if (fd < 0) {
    content = NULL;
    *fileSize = 0;
    fprintf(stderr, "file: "__FILE__", line: %d, " \
        "open file %s fail, " \
        "errno: %d, error info: %s", __LINE__, \
        filename, errno, strerror(errno));
    return errno != 0 ? errno : ENOENT;
  }

  if ((*fileSize=lseek(fd, 0, SEEK_END)) < 0) {
    content = NULL;
    *fileSize = 0;
    close(fd);
    fprintf(stderr, "file: "__FILE__", line: %d, " \
        "lseek file %s fail, " \
        "errno: %d, error info: %s", __LINE__, \
        filename, errno, strerror(errno));
    return errno != 0 ? errno : EIO;
  }

  content = (char *)malloc(*fileSize + 1);
  if (content == NULL) {
    *fileSize = 0;
    close(fd);

    fprintf(stderr, "file: "__FILE__", line: %d, " \
        "malloc %d bytes fail", __LINE__, \
        (int)(*fileSize + 1));
    return errno != 0 ? errno : ENOMEM;
  }

  if (lseek(fd, 0, SEEK_SET) < 0) {
    content = NULL;
    *fileSize = 0;
    close(fd);
    fprintf(stderr, "file: "__FILE__", line: %d, " \
        "lseek file %s fail, " \
        "errno: %d, error info: %s", __LINE__, \
        filename, errno, strerror(errno));
    return errno != 0 ? errno : EIO;
  }
  if (read(fd, content, *fileSize) != *fileSize) {
    free(content);
    content = NULL;
    *fileSize = 0;
    close(fd);
    fprintf(stderr, "file: "__FILE__", line: %d, " \
        "read from file %s fail, " \
        "errno: %d, error info: %s", __LINE__, \
        filename, errno, strerror(errno));
    return errno != 0 ? errno : EIO;
  }

  *(content + *fileSize) = '\0';
  close(fd);

  return 0;
}

