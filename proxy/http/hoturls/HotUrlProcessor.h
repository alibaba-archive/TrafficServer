#ifndef _HOT_URL_PROCESSOR_H
#define _HOT_URL_PROCESSOR_H

#include "I_HotUrls.h"

class HotUrlsHandler: public Continuation
{
  public:
    HotUrlsHandler();
    ~HotUrlsHandler() {}
    int main_event(int event, void *data);
  protected:
    time_t getLastReloadTime(const int interval);
};

class HotUrlProcessor : public Processor
{
  public:
    HotUrlProcessor() : _action(NULL) {}
    ~HotUrlProcessor() {}

    int init();

    int start(int number_of_threads = 0);

    void shutdown();

  private:
    // prevent unauthorized copies (Not implemented)
    HotUrlProcessor(const HotUrlProcessor &);
    HotUrlProcessor& operator=(const HotUrlProcessor &);

    HotUrlsHandler _handler;
    Action *_action;
};

extern HotUrlProcessor hotUrlProcessor;

#endif

