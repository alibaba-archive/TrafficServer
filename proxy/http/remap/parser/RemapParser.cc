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
#include "IncludeDirective.h"
#include "IncludeParams.h"

RemapParser::RemapParser()
{
  this->init();
}

RemapParser::~RemapParser()
{
  for (int i=0; i<_fileContents.count; i++) {
    free(_fileContents.items[i]);
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
  _rootDirective->_children[index++] = new IncludeDirective();
  _rootDirective->_childrenCount = index;
}

int RemapParser::loadFromFile(const char *filename, DirectiveParams *rootParams)
{
  int result;
  int fileSize;
  char *content;

  result = this->getFileContent(filename, content, &fileSize);
  if (result != 0) {
    return result;
  }

  _fileContents.add(content);
  rootParams->_directive = _rootDirective;
  return this->parse(rootParams, content, content + fileSize, true);
}

#define SKIP_COMMENT(p, count) \
  while (true) { \
    while ((p < contentEnd) && (*p == ' ' || *p == '\t')) { \
      p++; /* skip space */ \
    } \
    if ((p < contentEnd) && *p == '#') {  /* comment start */ \
      p++; /* skip # */   \
      while (p < contentEnd && *p != '\n') { \
        p++; \
      } \
      if (p < contentEnd && *p == '\n') { \
        count++; \
        p++; /* skip \n */ \
        continue; \
      } \
    } \
    \
    break; \
  }


int RemapParser::dealInclude(DirectiveParams *parentParams,
    IncludeParams *includeParams)
{
  char *content;
  int result;
  int fileSize;
  int i;
  const DynamicArray<char *> *filenames;
 
  parentParams->setLineNo(0);
  filenames = includeParams->getFilenames();
  for (i=0; i<filenames->count; i++) {
    result = this->getFileContent(filenames->items[i], content, &fileSize);
    if (result != 0) {
      return result;
    }

    _fileContents.add(content);
    parentParams->setFilename(filenames->items[i]);
    result = this->parse(parentParams, content, content + fileSize, false);
    if (result != 0) {
      return result;
    }
  }

  return 0;
}

int RemapParser::parse(DirectiveParams *params, char *content,
    char *contentEnd, const bool canInclude)
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
  int incRank;
  int rank;
  int result;
  char directiveName[MAX_DIRECTIVE_NAME_SIZE];
  bool bBlock;
  bool bAfterNewLine;

  lineNo = 0;
  incRank = 0;
  current = content;
  while (current < contentEnd) {
    line = current;
    getLine(current, contentEnd, lineEnd);
    lineNo++;
    incRank++;

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
      bAfterNewLine = true;
    }
    else {
      paramEnd = newLineEnd;
      blockStart = lineEnd;
      SKIP_COMMENT(blockStart, lineCount);
      while ((blockStart < contentEnd)) {
        if (*blockStart == ' ' || *blockStart == '\t' || *blockStart == '\r') {
          blockStart++;
        }
        else if (*blockStart == '\n') {
          lineCount++;
          blockStart++; // skip \n
          SKIP_COMMENT(blockStart, lineCount);
        }
        else {
          break;
        }
      }

      blockStatementStart = blockStart + 1;
      bBlock = (blockStart < contentEnd && *blockStart == '{');
      if (bBlock) {
        lineNo += lineCount;
      }
      lineCount = 0;  //reset
      bAfterNewLine = false;
    }

    if (bBlock) {
      blockEnd = blockStatementStart;
      if (bAfterNewLine) {
        SKIP_COMMENT(blockEnd, lineCount);
      }
      notMatchBlocks = 1;
      while (blockEnd < contentEnd) {
        if (*blockEnd == '}') {
          notMatchBlocks--;
          if (notMatchBlocks == 0) { //matched
            blockEnd++;
            break;
          }
          else if (notMatchBlocks < 0) {
            fprintf(stderr, "config file: %s, unexpect }, "
                "config line #%d: %.*s", params->_lineInfo.filename,
                params->_lineInfo.lineNo + lineNo,
                (int)(lineEnd - line), line);
            return EINVAL;
          }

          blockEnd++; //skip }
        }
        else if (*blockEnd == '{') {
          notMatchBlocks++;
          blockEnd++; //skip {
        }
        else if (*blockEnd == '\n') {
          lineCount++;
          blockEnd++; // skip \n
          SKIP_COMMENT(blockEnd, lineCount);
        }
        else {
          blockEnd++;
        }
      }

      if (notMatchBlocks != 0) {
        fprintf(stderr, "config file: %s, "
            "expect }, config line #%d: %.*s", params->_lineInfo.filename,
            params->_lineInfo.lineNo + lineNo, (int)(lineEnd - line), line);
        return EINVAL;
      }
    }
    else {
      blockStatementStart = NULL;
      blockEnd = NULL;
    }

    getToken(line, lineEnd, &tokenLen);
    if (tokenLen >= (int)sizeof(directiveName)) {
        fprintf(stderr, "config file: %s, "
            "ignore too long directive: %.*s, config line #%d: %.*s",
            params->_lineInfo.filename, tokenLen, line,
            params->_lineInfo.lineNo + lineNo, (int)(lineEnd - line), line);
        return EINVAL;
    }

    memcpy(directiveName, line, tokenLen);
    *(directiveName + tokenLen) = '\0';
    pChildDirective = params->_directive->getChild(directiveName);
    if (pChildDirective == NULL) {
      fprintf(stderr, "config file: %s, unkown directive: %s, "
          "config line #%d: %.*s", params->_lineInfo.filename,
          directiveName, params->_lineInfo.lineNo + lineNo,
          (int)(lineEnd - line), line);
      return EINVAL;
    }

    //fprintf(stderr, "%d %.*s\n", lineNo, tokenLen, line);

    //fprintf(stderr, "parent: %d, current: %d\n",
    //    params->_lineInfo.lineNo, lineNo);

    paramStr = line + tokenLen;
    while ((paramStr < paramEnd) && (*paramStr == ' ' || *paramStr == '\t')) {
      paramStr++;
    }

    if (params->getParent() == NULL) { //root
      rank = params->getRank() + incRank;
    }
    else {
      rank = params->getRank() + lineNo;
    }
    pChildParams = pChildDirective->newDirectiveParams(rank,
        params->getFilename(), params->getLineNo() + lineNo, line,
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
      if ((result=parse(pChildParams, blockStatementStart, blockEnd - 1,
              false)) != 0)
      {
        return result;
      }
    }

    if (params->getParent() == NULL) { //root
      params->incRank(incRank + lineCount);
      incRank = 0;
    }

    if (strcmp(pChildDirective->getName(), DIRECTVIE_NAME_INCLUDE) == 0) {
      const char *oldConfigFilename;
      int oldLineNo;

      if (!canInclude) {
        fprintf(stderr, "config file: %s, directive: %s can't occur here!"
            "config line #%d: %.*s", params->_lineInfo.filename,
            directiveName, params->_lineInfo.lineNo + lineNo,
            (int)(lineEnd - line), line);
        return EINVAL;
      }

      oldConfigFilename = params->getFilename();
      oldLineNo = params->getLineNo();
      result = this->dealInclude(params, (IncludeParams *)pChildParams);
      params->setFilename(oldConfigFilename);
      params->setLineNo(oldLineNo);
      if (result != 0) {
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

  if (params->getParent() == NULL && incRank > 0) { //root
    params->incRank(incRank);
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
    fprintf(stderr, "open file %s fail, "
        "errno: %d, error info: %s",
        filename, errno, strerror(errno));
    return errno != 0 ? errno : ENOENT;
  }

  if ((*fileSize=lseek(fd, 0, SEEK_END)) < 0) {
    content = NULL;
    *fileSize = 0;
    close(fd);
    fprintf(stderr, "call lseek file %s fail, "
        "errno: %d, error info: %s",
        filename, errno, strerror(errno));
    return errno != 0 ? errno : EIO;
  }

  content = (char *)malloc(*fileSize + 1);
  if (content == NULL) {
    *fileSize = 0;
    close(fd);

    fprintf(stderr, "malloc %d bytes fail",
        (int)(*fileSize + 1));
    return errno != 0 ? errno : ENOMEM;
  }

  if (lseek(fd, 0, SEEK_SET) < 0) {
    content = NULL;
    *fileSize = 0;
    close(fd);
    fprintf(stderr, "call lseek file %s fail, "
        "errno: %d, error info: %s",
        filename, errno, strerror(errno));
    return errno != 0 ? errno : EIO;
  }
  if (read(fd, content, *fileSize) != *fileSize) {
    free(content);
    content = NULL;
    *fileSize = 0;
    close(fd);
    fprintf(stderr, "read from file %s fail, "
        "errno: %d, error info: %s",
        filename, errno, strerror(errno));
    return errno != 0 ? errno : EIO;
  }

  *(content + *fileSize) = '\0';
  close(fd);

  return 0;
}

