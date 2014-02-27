#include "HotUrlProcessor.h"
#include "HotUrlManager.h"
#include "HotUrlStats.h"

extern volatile int64_t cluster_current_out_bps;

HotUrlStats *HotUrlStats::instance = new HotUrlStats();

HotUrlStats::HotUrlStats()
: _detect(false),
  _total_send_bytes(0),
  _total_query_count(0),
  _current_send_bps(0),
  _current_qps(0.00)
{
  _currentHotUrlMap = _hotUrlMaps;
}

void HotUrlStats::doCalcSendBps()
{
#define RETRY_COUNT   10
  static int counter = 0;
  int old_counter;
  static int64_t last_send_bytes = 0;
  static int64_t last_query_count = 0;
  static ink_hrtime last_calc_time = ink_get_hrtime();
  ink_hrtime current_time;
  double delta_time;

  current_time = ink_get_hrtime();
  delta_time = (double)(current_time - last_calc_time) / (double)HRTIME_SECOND;
  if (delta_time < 0.001) {
    return;
  }

  _current_send_bps = (int64_t)(8 * (_total_send_bytes -
        last_send_bytes) / delta_time);
  last_send_bytes = _total_send_bytes;

  _current_qps = (_total_query_count - last_query_count) / delta_time;
  last_query_count = _total_query_count;
  last_calc_time = current_time;

  if (_config.max_count == 0) {
    return;
  }

  old_counter = counter;
  counter = 0;
  bool doDetect = true;
  if (HotUrlManager::getHotUrlCount() > 0) {
    if (3 * _current_send_bps / 2 < _config.detect_on_bps) {
      doDetect = false;
    }
  }
  else {
    if (_current_send_bps < _config.detect_on_bps) {
      if (_detect) {
        if ((int64_t)(_current_send_bps * (1.00 +
                (RETRY_COUNT - old_counter++) / 100.00)) < _config.detect_on_bps)
        {
          doDetect = false;
        }
        else {
          counter = old_counter;
        }
      }
      else {
        doDetect = false;
      }
    }
    else if (_config.detect_on_bps_ratio > 0.0001) {
      double ratio;
      if (_current_send_bps == cluster_current_out_bps) {
        ratio = 1.00;
      }
      else if (_current_send_bps > cluster_current_out_bps) {
        ratio = (double)cluster_current_out_bps / (double)_current_send_bps;
      }
      else {
        ratio = (double)_current_send_bps / (double)cluster_current_out_bps;
      }

      if (ratio > _config.detect_on_bps_ratio) {
        if (_detect) {
          if (ratio * (1.00 - (RETRY_COUNT - old_counter++) / 100.00) >
              _config.detect_on_bps_ratio)
          {
            doDetect = false;
          }
          else {
            counter = old_counter;
          }
        }
        else {
          doDetect = false;
        }
      }
    }
  }

  setDetect(doDetect);
}

void HotUrlStats::doCalcHotUrls()
{
  if (!_detect) {
    return;
  }

  static ink_hrtime last_calc_time = ink_get_hrtime();
  ink_hrtime current_time;
  double delta_time;

  current_time = ink_get_hrtime();
  delta_time = (double)(current_time - last_calc_time) / (double)HRTIME_SECOND;
  if (delta_time < 0.99) {
    return;
  }

  int64_t current_send_bytes = _current_send_bps / 8;
  double current_qps  = _current_qps;
  if (current_send_bytes == 0 || current_qps < 0.0001) {
    if (HotUrlManager::getHotUrlCount() > 0) {
      HotUrlManager::getHotUrlArray()->clear();
    }
    last_calc_time = current_time;
    return;
  }

  HotUrlMap *OldUrlMap;
  HotUrlMap *newUrlMap;
  OldUrlMap = _currentHotUrlMap;
  if (_currentHotUrlMap == _hotUrlMaps) {
    newUrlMap = _hotUrlMaps + 1;
  }
  else {
    newUrlMap = _hotUrlMaps;
  }
  newUrlMap->clear();
  _currentHotUrlMap = newUrlMap;
  last_calc_time = current_time;

  HotUrlMap::PriorityQueue *urlQueue = OldUrlMap->getUrlQueue();
  const HotUrlMap::UrlMapEntry *head;
  const HotUrlMap::UrlMapEntry *lastMatchEntry = NULL;
  bool matched;
  int i;

  i = 0;
  head = urlQueue->head();
  while (head != NULL) {
    matched = false;
    if (_config.detect_type & HOT_URLS_DETECT_TYPE_BYTES) {
      if (((double)head->_bytes / delta_time) / (double)current_send_bytes >=
          _config.single_url_select_ratio)
      {
        lastMatchEntry = head;
        matched = true;
        Debug(HOT_URLS_DEBUG_TAG, "single %d. %.*s, bytes=%ld, "
            "ratio=%.2f, qps=%.2f", i + 1, head->_url.length, head->_url.url,
            head->_bytes, ((double)head->_bytes / delta_time) /
            (double)current_send_bytes, (double)head->_count / delta_time);
      }
    }
    else {
      if ((head->_count / delta_time) / current_qps >= _config.single_url_select_ratio) {
        lastMatchEntry = head;
        matched = true;
        Debug(HOT_URLS_DEBUG_TAG, "single %d. %.*s, count=%d, "
            "ratio=%.2f, qps=%.2f", i + 1, head->_url.length, head->_url.url,
            head->_count, ((double)head->_count / delta_time) / current_qps,
            (double)head->_count / delta_time);
      }
    }

    if (!matched) {
      break;
    }

    head = head->_next;
    ++i;
  }

  if (lastMatchEntry == NULL && _config.multi_url_select_ratio > 0.0001) {
    int64_t bytes_sum = 0;
    int64_t count_sum = 0;
    i = 0;
    head = urlQueue->head();
    while (head != NULL) {
      if (_config.detect_type & HOT_URLS_DETECT_TYPE_BYTES) {
        bytes_sum += head->_bytes;
        if ((double)bytes_sum / delta_time / (double)current_send_bytes >=
            _config.multi_url_select_ratio) {
          lastMatchEntry = head;
          Debug(HOT_URLS_DEBUG_TAG, "multi %d. %.*s: %ld", i + 1,
              head->_url.length, head->_url.url, head->_bytes);
          break;
        }
      }
      else {
        count_sum += head->_count;
        if ((double)count_sum / delta_time / current_qps >= _config.multi_url_select_ratio) {
          lastMatchEntry = head;
          Debug(HOT_URLS_DEBUG_TAG, "multi %d. %.*s: %d", i + 1,
              head->_url.length, head->_url.url, head->_count);
          break;
        }
      }

      head = head->_next;
      ++i;
    }
  }

  if (lastMatchEntry == NULL) {
    if (HotUrlManager::getHotUrlCount() > 0) {
      HotUrlManager::getHotUrlArray()->clear();
    }
  }
  else {
    HotUrlManager::getHotUrlArray()->replace(urlQueue->head(), lastMatchEntry);
  }
}

void HotUrlStats::setDetect(const bool detect)
{
  if (_detect != detect) {
    if (_detect) {
      if (HotUrlManager::getHotUrlCount() > 0) {
        HotUrlManager::getHotUrlArray()->clear();
      }
      Debug(HOT_URLS_DEBUG_TAG, "disable hot url detect.");
    }
    else {
      Debug(HOT_URLS_DEBUG_TAG, "enable hot url detect.");
    }
    _detect = detect;
  }
}

void HotUrlStats::setMaxCount(const uint32_t maxCount)
{
  for (int i=0; i<2; i++) {
    _hotUrlMaps[i].getUrlQueue()->setMaxCount(maxCount);
  }

  int oldMaxCount = _config.max_count;
  _config.max_count = maxCount;

  if (oldMaxCount == 0) {
    if (maxCount > 0) {
      hotUrlProcessor.start();
    }
  }
  else {
    if (maxCount == 0) {
      if (oldMaxCount > 0) {
        setDetect(false);
        hotUrlProcessor.shutdown();
        if (HotUrlManager::getHotUrlCount() > 0) {
          HotUrlManager::getHotUrlArray()->clear();
        }
      }
    }
  }
  Debug(HOT_URLS_DEBUG_TAG, "hot url max_count: %u", instance->_config.max_count);
}

void HotUrlStats::setDetectInverval(const int interval)
{
  if (interval > 0) {
    _config.detect_interval = HRTIME_SECONDS(interval);
    Debug(HOT_URLS_DEBUG_TAG, "hot url detect_interval: %ds",
        (int)(_config.detect_interval / HRTIME_SECOND));
  }
}

void HotUrlStats::setKeepDays(const int days)
{
  _config.keep_time = days * 86400;
  Debug(HOT_URLS_DEBUG_TAG, "hot url keep_days: %u", _config.keep_time / 86400);
}

int HotUrlStats::configChangeCallback(const char *name, RecDataT data_type,
    RecData data, void *cookie)
{
  NOWARN_UNUSED(data_type);
  NOWARN_UNUSED(cookie);

  if (strcmp(name, "proxy.config.http.hoturls.max_count") == 0) {
    instance->setMaxCount(data.rec_int);
  }
  else if (strcmp(name, "proxy.config.http.hoturls.detect_interval_secs") == 0) {
    instance->setDetectInverval(data.rec_int);
  }
  else if (strcmp(name, "proxy.config.http.hoturls.keep_days") == 0) {
    instance->setKeepDays(data.rec_int);
  }

  return 0;
}

uint32_t HotUrlStats::getKeepTime()
{
  if (instance->_config.keep_time == 0) {
    int keep_days = (int)REC_ConfigReadInteger("proxy.config.http.hoturls.keep_days");
    instance->setKeepDays(keep_days);
  }
  return instance->_config.keep_time;
}

void HotUrlStats::init()
{
  int interval = (int)REC_ConfigReadInteger("proxy.config.http.hoturls.detect_interval_secs");
  instance->setDetectInverval(interval);
  REC_RegisterConfigUpdateFunc("proxy.config.http.hoturls.detect_interval_secs", configChangeCallback, NULL);

  getKeepTime();
  REC_RegisterConfigUpdateFunc("proxy.config.http.hoturls.keep_days", configChangeCallback, NULL);

  REC_EstablishStaticConfigInt32(instance->_config.detect_type, "proxy.config.http.hoturls.detect_type");
  REC_EstablishStaticConfigInteger(instance->_config.detect_on_bps, "proxy.config.http.hoturls.detect_on_bps");
  REC_EstablishStaticConfigFloat(instance->_config.detect_on_bps_ratio, "proxy.config.http.hoturls.detect_on_bps_ratio");
  REC_EstablishStaticConfigFloat(instance->_config.single_url_select_ratio, "proxy.config.http.hoturls.single_url_select_ratio");
  REC_EstablishStaticConfigFloat(instance->_config.multi_url_select_ratio, "proxy.config.http.hoturls.multi_url_select_ratio");

  Debug(HOT_URLS_DEBUG_TAG, "hot url detect_type: %d, detect_on_bps: %ld, detect_on_bps_ratio: %.4f",
      instance->_config.detect_type, instance->_config.detect_on_bps, instance->_config.detect_on_bps_ratio);
  Debug(HOT_URLS_DEBUG_TAG, "hot url single_url_select_ratio: %.4f, multi_url_select_ratio: %.4f",
      instance->_config.single_url_select_ratio, instance->_config.multi_url_select_ratio);

  uint32_t maxCount = (uint32_t)REC_ConfigReadInteger("proxy.config.http.hoturls.max_count");
  instance->setMaxCount(maxCount);
  REC_RegisterConfigUpdateFunc("proxy.config.http.hoturls.max_count", configChangeCallback, NULL);
}

