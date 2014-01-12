
#ifndef __P_SPDY_SM_H__
#define __P_SPDY_SM_H__

#include "P_SpdyCommon.h"
#include "P_SpdyCallbacks.h"
#include <openssl/md5.h>


class SpdySM;
typedef int (*SpdySMHandler) (TSCont contp, TSEvent event, void *data);


class SpdyRequest
{
public:
  SpdyRequest(SpdySM *sm, int id):
    spdy_sm(sm), stream_id(id), fetch_sm(NULL),
    has_submitted_data(false), need_resume_data(false),
    fetch_data_len(0), delta_window_size(0),
    fetch_body_completed(false)
  {
    MD5_Init(&recv_md5);
    start_time = TShrtime();
  }

  ~SpdyRequest();

  void append_nv(char **nv)
  {
    for(int i = 0; nv[i]; i += 2) {
      headers.push_back(make_pair(nv[i], nv[i+1]));
    }
  }

public:
  int event;
  SpdySM *spdy_sm;
  int stream_id;
  TSHRTime start_time;
  TSFetchSM fetch_sm;
  bool has_submitted_data;
  bool need_resume_data;
  int fetch_data_len;
  int delta_window_size;
  bool fetch_body_completed;
  vector<pair<string, string> > headers;

  string url;
  string host;
  string path;
  string scheme;
  string method;
  string version;

  MD5_CTX recv_md5;
};

class SpdySM
{

public:

  SpdySM(TSVConn conn);
  ~SpdySM();

  void init();

public:

  int64_t sm_id;
  uint64_t total_size;
  TSHRTime start_time;

  TSVConn net_vc;
  TSCont  contp;

  TSIOBuffer req_buffer;
  TSIOBufferReader req_reader;

  TSIOBuffer resp_buffer;
  TSIOBufferReader resp_reader;

  TSVIO   read_vio;
  TSVIO   write_vio;

  SpdySMHandler current_handler;

  int event;
  spdylay_session *session;

  map<int32_t, SpdyRequest*> req_map;
};


void spdy_sm_create(TSVConn cont);

#endif

