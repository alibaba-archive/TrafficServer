#include "HotUrlStats.h"
#include "HotUrlHistory.h"
#include "HotUrlProcessor.h"

HotUrlProcessor hotUrlProcessor;

HotUrlsHandler::HotUrlsHandler()
{
  SET_HANDLER(&HotUrlsHandler::main_event);
}

time_t HotUrlsHandler::getLastReloadTime(const int interval)
{
  struct tm tm;
  time_t currentTime = time(NULL);
  time_t t = currentTime - 86400;

  localtime_r(&t, &tm);
  tm.tm_sec = 0;
  tm.tm_min = 0;
  tm.tm_hour = 2;

  time_t lastReloadTime = mktime(&tm);
  while (lastReloadTime + interval <= currentTime) {
    lastReloadTime += interval;
  }

  return lastReloadTime;
}

int HotUrlsHandler::main_event(int event, void *data)
{
#define RELOAD_INTERVAL  86400

  static ink_hrtime last_reload_time = HRTIME_SECONDS(
      getLastReloadTime(RELOAD_INTERVAL));
  ink_hrtime current_time = ink_get_hrtime();

  HotUrlStats::calcSendBps();
  HotUrlStats::calcHotUrls();

  if (last_reload_time + HRTIME_SECONDS(RELOAD_INTERVAL) <= current_time) {
    HotUrlHistory::reload();
    last_reload_time = current_time;
  }

  return 0;
}

int HotUrlProcessor::init()
{
  int result;
  char filename[256];
  _handler.mutex = new_ProxyMutex();
  snprintf(filename, sizeof(filename), "%s/hoturl.binlog",
      system_runtime_dir);
  result = HotUrlHistory::load(filename);
  HotUrlStats::init();
  return result;
}

int HotUrlProcessor::start(int number_of_threads)
{
  ink_release_assert(_action == NULL);
  _action = eventProcessor.schedule_every(&_handler, HotUrlStats::getDetectInterval(), ET_TASK);
  Note("HotUrlProcessor start.");
  return EXIT_SUCCESS;
}

void HotUrlProcessor::shutdown()
{
  if (_action != NULL) {
    _action->cancel();
    _action = NULL;
  }
  Note("HotUrlProcessor stop.");
}

