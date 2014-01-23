#ifndef _HOT_URL_STATS_H
#define _HOT_URL_STATS_H

#include "I_HotUrls.h"
#include "HotUrlMap.h"

#define HOT_URLS_DEBUG_TAG "hoturls"

struct HotUrlConfig {
  uint32_t keep_time;   //for purge
  uint32_t max_count;
  uint64_t detect_interval;
  int64_t detect_on_bps;
  float detect_on_bps_ratio;
  float single_url_bps_ratio;
  float multi_url_bps_ratio;
  
  HotUrlConfig()
    : keep_time(0), max_count(0), detect_interval(HRTIME_SECONDS(1)),
    detect_on_bps(0), detect_on_bps_ratio(0.00), single_url_bps_ratio(0.00),
    multi_url_bps_ratio(0.00)
  {
  }
};

class HotUrlStats {
  public:
    ~HotUrlStats();

    static inline HotUrlStats *getInstance() {
      return instance;
    }

    static void init();

    static inline void calcSendBps() {
      instance->doCalcSendBps();
    }

    static inline void calcHotUrls() {
      instance->doCalcHotUrls();
    }

    static inline void incSendBytes(const char *url, const int url_len,
        const int64_t bytes)
    {
      if (instance->_config.max_count > 0) {
        ink_atomic_increment64(&instance->_total_send_bytes, bytes);
        if (instance->_detect) {
          instance->_currentHotUrlMap->incrementBytes(url, url_len, bytes);
        }
      }
    }

    static inline int64_t getDetectInterval() {
      return instance->_config.detect_interval;
    }

    static uint32_t getKeepTime();

    static inline uint32_t getMaxCount() {
      return instance->_config.max_count;
    }

    static inline bool enabled() {
      return instance->_config.max_count > 0;
    }

    inline HotUrlConfig *getConfig() {
      return &_config;
    }

    static int configChangeCallback(const char *name, RecDataT data_type,
        RecData data, void *cookie);

  public:
    static HotUrlStats *instance;

  protected:
    void setMaxCount(const uint32_t maxCount);
    void setDetectInverval(const int interval);
    void setKeepDays(const int days);
    void setDetect(const bool detect);

    HotUrlStats();
    void doCalcSendBps();
    void doCalcHotUrls();

  private:
    bool _detect;
    volatile int64_t _total_send_bytes;
    volatile int64_t _current_send_bps;
    HotUrlConfig _config;
    HotUrlMap _hotUrlMaps[2];
    HotUrlMap *_currentHotUrlMap;
};

#endif

