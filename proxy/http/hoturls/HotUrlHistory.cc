#include "HotUrlStats.h"
#include "HotUrlHistory.h"

HotUrlHistory::UrlArray::UrlArray()
  : _urls(NULL),
  _allocSize(0),
  _count(0)
{
}

HotUrlHistory::UrlArray::~UrlArray()
{
  if (_urls != NULL) {
    ats_free(_urls);
    _urls = NULL;
  }
  _allocSize = 0;
  _count = 0;
}

HotUrlHistory::HotUrlEntry *HotUrlHistory::UrlArray::equalLarge(
    HotUrlHistory::HotUrlEntry *target, bool *bEqual)
{
  HotUrlEntry *left = _urls;
  HotUrlEntry *right = _urls + _count - 1;
  HotUrlEntry *mid;
  int compResult;

  while (left <= right) {
    mid = _urls + ((right - _urls) +  (left - _urls)) / 2;
    compResult = HotUrlEntry::compare(target, mid);
    if (compResult < 0) {
      right = mid - 1;
    }
    else if (compResult == 0) {
      *bEqual = true;
      return mid;
    }
    else {
      left = mid + 1;
    }
  }

  *bEqual = false;
  return left;
}

int HotUrlHistory::UrlArray::add(const char *url, const int length,
    const time_t createTime)
{
  HotUrlEntry *entry;

  if (length >= MAX_URL_SIZE) {
    return ENAMETOOLONG;
  }

  if (_count >= _allocSize) {
    int bytes;
    int allocSize = _allocSize == 0 ? 64 : 2 *_allocSize;
    bytes = sizeof(HotUrlEntry) * allocSize;
    HotUrlEntry *urls = (HotUrlEntry *)ats_realloc(_urls, bytes);
    if (urls == NULL) {
      return ENOMEM;
    }

    _urls = urls;
    _allocSize = allocSize;
  }

  entry = _urls + _count;
  memcpy(entry->url, url, length);
  entry->length = length;

  bool bEqual;
  HotUrlEntry *found = equalLarge(entry, &bEqual);
  if (bEqual) {
    if (found->createTime < createTime) {
      found->createTime = createTime;
    }
    return EEXIST;
  }

  if (found == entry) { //the last position, do NOT remove others
    entry->createTime = createTime;
    ++_count;
    return 0;
  }

  while (entry > found) {
    memcpy(entry, entry - 1, sizeof(HotUrlEntry));
    entry--;
  }

  memcpy(entry->url, url, length);
  entry->length = length;
  entry->createTime = createTime;
  ++_count;

  return 0;
}

HotUrlHistory::HotUrlEntry *HotUrlHistory::UrlArray::find(const char *url,
    const int length)
{
  HotUrlEntry target;

  if (length >= MAX_URL_SIZE) {
    return NULL;
  }

  if (_count == 0) {
    return NULL;
  }

  memcpy(target.url, url, length);
  target.length = length;
  return (HotUrlEntry *)bsearch(&target, _urls, _count,
      sizeof(HotUrlEntry), HotUrlEntry::compare);
}

HotUrlHistory *HotUrlHistory::instance = new HotUrlHistory;

int HotUrlHistory::doAdd(const char *url, const int length,
        const time_t createTime)
{
  char buff[MAX_URL_SIZE + 32];
  int bytes;

  if (length >= MAX_URL_SIZE) {
    return ENAMETOOLONG;
  }

  bytes = sprintf(buff, "%d %.*s\n", (int)createTime, length, url);
  if (write(_fd, buff, bytes) != bytes) {
    Warning("write to file %s fail, errno: %d, error info: %s", _filename,
        errno, strerror(errno));
  }
  else if (fsync(_fd) != 0) {
    Warning("sync to file %s fail, errno: %d, error info: %s", _filename,
        errno, strerror(errno));
  }

  return _currentHotUrls.add(url, length, createTime);
}

int HotUrlHistory::openBinlog()
{
  int result;
  _fd = open(_filename, O_WRONLY | O_APPEND | O_CREAT, 0664);
  if (_fd < 0) {
    result = errno != 0 ? errno : EPERM;
    Error("open file %s fail, errno: %d, error info: %s",
        _filename, result, strerror(result));
    return result;
  }

  return 0;
}

class CacheRemoveHandler: public Continuation
{
  public:
    CacheRemoveHandler();
    ~CacheRemoveHandler() {}
    int main_event(int event, void *data);
};

static ClassAllocator<CacheRemoveHandler> cacheRemoveAllocator("CacheRemoveHandler");

CacheRemoveHandler::CacheRemoveHandler()
{
  SET_HANDLER(&CacheRemoveHandler::main_event);
}

int CacheRemoveHandler::main_event(int event, void *data)
{
  switch (event) {
    case CACHE_EVENT_REMOVE:
    case CACHE_EVENT_REMOVE_FAILED:
      cacheRemoveAllocator.free(this);
      break;
    default:
      Warning("invalid event: %d", event);
      break;
  }

  return 0;
}

void HotUrlHistory::removeCache(HdrHeap *hdrHeap, const char *url, const int length)
{
  const bool cluster_cache_local = true;
  const char *hostname;
  int host_len;
  INK_MD5 cache_key;
  URL theURL;

  theURL.create(hdrHeap);
  do {
    if (theURL.parse(url, length) == PARSE_ERROR) {
      Warning("parse error, url: %.*s", length, url);
      break;
    }

    theURL.MD5_get(&cache_key);
    if (cacheProcessor.belong_to_me(&cache_key)) {
      break;
    }

    CacheRemoveHandler *cont = cacheRemoveAllocator.alloc();
    cont->mutex = new_ProxyMutex();

    hostname = theURL.host_get(&host_len);
    cacheProcessor.remove(cont, &cache_key,
        cluster_cache_local, CACHE_FRAG_TYPE_HTTP, true, false,
        (char *)hostname, host_len);
    //Note("remove url: %s", url);
  } while (0);

  theURL.clear();
  return;
}

int HotUrlHistory::doLoad()
{
  int result;
  FILE *fp;
  char line[MAX_URL_SIZE + 32];
  UrlEntry urlEntry;
  int createTime;
  time_t currentTime;

  result = 0;
  do {
    if (access(_filename, F_OK) != 0) {
      if (errno != ENOENT) {
        result = errno != 0 ? errno : EPERM;
        Error("access to file %s fail, errno: %d, error info: %s",
            _filename, result, strerror(result));
        return result;
      }
      break;
    }

    fp = fopen(_filename, "r");
    if (fp == NULL) {
      result = errno != 0 ? errno : EPERM;
      Error("open file %s fail, errno: %d, error info: %s",
          _filename, result, strerror(result));
      return result;
    }

    currentTime = (time_t)(ink_get_hrtime() / HRTIME_SECOND);
    while (fgets(line, sizeof(line), fp) != NULL) {
      if (sscanf(line, "%d %s\n", &createTime, urlEntry.url) != 2) {
        Warning("read file %s fail, line: %s", _filename, line);
        continue;
      }

      urlEntry.length = strlen(urlEntry.url);
      _currentHotUrls.add(&urlEntry, createTime);
    }

    fclose(fp);
  } while (0);

  Debug(HOT_URLS_DEBUG_TAG, "reload done, url count: %d",
      _currentHotUrls.getCount());

  if (result == 0) {
    return openBinlog();
  }

  openBinlog();
  return result;
}

int HotUrlHistory::doDump()
{
  char tmpFilename[sizeof(_filename) + 32];
  int result;
  FILE *fp;
  uint32_t keepTime;
  time_t currentTime;

  sprintf(tmpFilename, "%s.tmp", _filename);
  if (access(tmpFilename, F_OK) == 0) {
    if (unlink(tmpFilename) != 0) {
      result = errno != 0 ? errno : EPERM;
      Error("unlink file %s fail, errno: %d, error info: %s",
          _filename, result, strerror(result));
      return result;
    }
  }

  fp = fopen(tmpFilename, "w");
  if (fp == NULL) {
    result = errno != 0 ? errno : EPERM;
    Error("open file %s fail, errno: %d, error info: %s",
        tmpFilename, result, strerror(result));
    return result;
  }

  HdrHeap *hdrHeap = new_HdrHeap();
  keepTime = HotUrlStats::getKeepTime();
  currentTime = (time_t)(ink_get_hrtime() / HRTIME_SECOND);
  HotUrlEntry *urls = _currentHotUrls.getUrls();
  HotUrlEntry *urlEnd = urls + _currentHotUrls.getCount();
  HotUrlEntry *pEntry;
  for (pEntry=urls; pEntry<urlEnd; pEntry++) {
    if (pEntry->createTime + keepTime < currentTime) {  //expires
      removeCache(hdrHeap, pEntry->url, pEntry->length);
      continue;
    }

    if (fprintf(fp, "%d %.*s\n", (int)pEntry->createTime,
          pEntry->length, pEntry->url) <= 0)
    {
      result = errno != 0 ? errno : EPERM;
      Error("write to file %s fail, errno: %d, error info: %s",
          tmpFilename, result, strerror(result));
      fclose(fp);
      hdrHeap->destroy();
      return result;
    }
  }

  hdrHeap->destroy();

  fclose(fp);
  close(_fd);
  _fd = -1;

  if (rename(tmpFilename, _filename) != 0) {
      result = errno != 0 ? errno : EPERM;
      Error("rename file %s to %s fail, errno: %d, error info: %s",
          tmpFilename, _filename, result, strerror(result));
      openBinlog();
      return result;
  }

  return 0;
}

int HotUrlHistory::doReload()
{

  if (cacheProcessor.IsCacheEnabled() != CACHE_INITIALIZED) {
    Warning("cache not ready!");
    return EBUSY;
  }

  int result;
  if ((result=doDump()) != 0) {
    return result;
  }

  if (_fd >= 0) {
    close(_fd);
    _fd = -1;
  }
  _currentHotUrls.clear();
  result = doLoad();
  return result;
}

