#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <pcre.h>
#include "IncludeParams.h"

IncludeParams::IncludeParams(const int rank, const char *filename, const int lineNo,
    const char *lineStr, const int lineLen, DirectiveParams *parent,
    RemapDirective *directive, const char *paramStr,
    const int paramLen, const bool bBlock) :
  DirectiveParams(rank, filename, lineNo, lineStr, lineLen, parent, directive,
      paramStr, paramLen, bBlock)
{
  *_rawFilename = '\0';
}

IncludeParams::~IncludeParams()
{
  for (int i=0; i<_filenames.count; i++) {
    free(_filenames.items[i]);
  }
}

int IncludeParams::parse(const char *blockStart, const char *blockEnd)
{
  snprintf(_rawFilename, sizeof(_rawFilename), "%.*s",
      _params[0].length, _params[0].str);
  return expandFilenames();
}

const char *IncludeParams::toString(char *buff, int *len)
{
  *len = sprintf(buff, "%s %s", _directive->getName(), _rawFilename);
  return (const char *)buff;
}

int IncludeParams::listFile(const char *filepath, const char *filename)
{
  DIR *dir;
  pcre *re;
  struct dirent entry;
  struct dirent *pEntry;
  const char *pSrc;
  char *pDest;
  char pattern[1024];
  char fullFilename[1024];
  struct stat fileStat;
  const char *str;
  int result;
  int str_index;
  int matches[3];
  int match_result;

  if ((int)(strlen(filename) * 2) + 2 >= (int)sizeof(pattern)) {
      fprintf(stderr, "config file: %s, "
          "config line no: %d, filename: %s is too long! ",
          _lineInfo.filename, _lineInfo.lineNo, filename);
    return EINVAL;
  }

  pSrc = filename;
  pDest = pattern;

  *pDest++ = '^';
  while (*pSrc != '\0') {
    switch (*pSrc) {
      case '?':
        *pDest++ = '.';
        break;
      case '*':
        *pDest++ = '.';
        *pDest++ = '*';
        break;
      case '.':
        *pDest++ = '\\';
        *pDest++ = '.';
        break;
      default:
        *pDest++ = *pSrc;
        break;
    }
    pSrc++;
  }
  *pDest++ = '$';
  *pDest = '\0';

  re = pcre_compile(pattern, 0, &str, &str_index, NULL);
  if (re == NULL) {
      fprintf(stderr, "config file: %s, "
          "config line no: %d, pcre_compile failed! "
          "Regex has error starting at %s", _lineInfo.filename,
          _lineInfo.lineNo, pattern + str_index);
    return EINVAL;
  }

  if ((dir=opendir(filepath)) == NULL) {
    result = errno != 0 ? errno : ENOENT;
    fprintf(stderr, "config file: %s, "
        "config line no: %d, open path: %s fail,"
        "errno: %d, error info: %s\n", _lineInfo.filename, _lineInfo.lineNo,
        filepath, result, strerror(result));
    pcre_free(re);
    return result;
  }

  while (readdir_r(dir, &entry, &pEntry) == 0) {
    if (pEntry == NULL) {
      break;
    }

    if (*(pEntry->d_name) == '.' && (*(pEntry->d_name + 1) == '\0' ||
          (*(pEntry->d_name + 1) == '.' && *(pEntry->d_name + 2) == '\0')))
    {
      continue;
    }

    match_result = pcre_exec(re, NULL, pEntry->d_name,
        strlen(pEntry->d_name), 0, 0, matches,
        (sizeof(matches) / sizeof(int)));
    if (match_result <= 0) {
      continue;
    }

    snprintf(fullFilename, sizeof(fullFilename), "%s%s",
        filepath, pEntry->d_name);
    if (stat(fullFilename, &fileStat) == 0) {
      if (S_ISREG(fileStat.st_mode)) {
        _filenames.add(strdup(fullFilename));
      }
    }
  }

  pcre_free(re);
  closedir(dir);
  return 0;
}

int IncludeParams::expandFilenames()
{
  int result;
  struct stat fileStat;
  char *filename;
  char filepath[1024];

  if (stat(_rawFilename, &fileStat) == 0) {
    if (S_ISREG(fileStat.st_mode)) {
      _filenames.add(strdup(_rawFilename));
      return 0;
    }
    else {
      fprintf(stderr, "config file: %s, "
          "config line no: %d, %s not a regular file\n", _lineInfo.filename,
          _lineInfo.lineNo, _rawFilename);
      return EINVAL;
    }
  }

  result = errno != 0 ? errno : ENOENT;
  filename = strrchr(_rawFilename, '/');
  if (filename == NULL) {
      fprintf(stderr, "config file: %s, "
          "config line no: %d, invalid filename: %s, "
          "errno: %d, error info: %s\n", _lineInfo.filename,_lineInfo.lineNo,
          _rawFilename, result, strerror(result));
      return EINVAL;
  }

  filename++; //skip /
  if (strchr(filename, '*') == NULL && strchr(filename, '?') == NULL) {
      fprintf(stderr, "config file: %s, "
          "config line no: %d, stat filename: %s fail, "
          "errno: %d, error info: %s\n", _lineInfo.filename, _lineInfo.lineNo,
          _rawFilename, result, strerror(result));
      return ENOENT;
  }

  snprintf(filepath, sizeof(filepath), "%.*s",
      (int)(filename - _rawFilename), _rawFilename);
  return listFile(filepath, filename);
}

