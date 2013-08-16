
#ifndef _CLUSTER_INTERFACE_H
#define _CLUSTER_INTERFACE_H

struct ClusterMachine;
class IOBufferData;
class IOBufferBlock;

#define CLUSTER_DEBUG_TAG "cluster_io"
#define USE_CLUSTER_TIME  1

#define new_RecvBuffer(len) \
  new_IOBufferData(iobuffer_size_to_index(len, MAX_BUFFER_SIZE_INDEX))

#ifdef USE_CLUSTER_TIME
#define CURRENT_TIME() (cluster_current_time / HRTIME_SECOND)
#define CURRENT_MS() (cluster_current_time / HRTIME_MSECOND)
#define CURRENT_NS() (cluster_current_time / HRTIME_NSECOND)
#else
#define CURRENT_TIME() (ink_get_hrtime() / HRTIME_SECOND)
#define CURRENT_MS() (ink_get_hrtime() / HRTIME_MSECOND)
#define CURRENT_NS() (ink_get_hrtime() / HRTIME_NSECOND)
#endif

#define MINI_MESSAGE_SIZE     64  //use internal buffer to store the mini message

#define FUNC_ID_CONNECTION_CLOSED_NOTIFY 6100   //connection closed
#define FUNC_ID_CLUSTER_PING_REQUEST     6201
#define FUNC_ID_CLUSTER_PING_RESPONSE    6202
#define FUNC_ID_CLUSTER_HELLO_REQUEST    6203
#define FUNC_ID_CLUSTER_HELLO_RESPONSE   6204

#define RESPONSE_EVENT_NOTIFY_DEALER 1

typedef uint64_t SequenceType;

typedef union {
	struct {
    uint32_t ip;    //src ip addr
		uint32_t timestamp;  //session create time
		SequenceType seq;    //session sequence number
	} fields;

	uint64_t ids[2]; //session id, 0 for free entry
} SessionId;

typedef SessionId ClusterSession;

typedef enum {
  PRIORITY_HIGH = 0,
  PRIORITY_MID,
  PRIORITY_LOW,
} MessagePriority;


#define CLEAR_SESSION(session_id) \
  (session_id).ids[0] = (session_id).ids[1] = 0

typedef int (*machine_change_notify_func)(ClusterMachine * m);

typedef void (*message_deal_func)(ClusterSession session, void *arg,
    const int func_id, IOBufferBlock *blocks, const int data_len);

/*
typedef void (*message_deal_func)(ClusterSession session, void *arg,
    const int func_id, void *data, int data_len);
*/

int cluster_global_init(message_deal_func deal_func,
    machine_change_notify_func machine_change_notify);

int cluster_create_session(ClusterSession *session,
    const struct ClusterMachine *machine, void *arg, const int events);

int cluster_bind_session(ClusterSession session, void *arg);

int cluster_set_events(ClusterSession session, const int events);

void *cluster_close_session(ClusterSession session);

/*
 * data pointer as:
 *    data_len: -1 for IOBufferBlock *, >= 0 for char buffer
 **/
int cluster_send_message(ClusterSession session, const int func_id,
	void *data, const int data_len, const MessagePriority priority);

#endif

