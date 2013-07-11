//machine.h

#ifndef _MACHINE_H
#define _MACHINE_H

#include "common_define.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned int g_my_machine_index;
extern unsigned int g_my_machine_ip;
extern int g_my_machine_id;
extern int g_machine_count;
extern struct ClusterMachine *g_machines;

int init_machines();
int start_machines_connection();
ClusterMachine *add_machine(const unsigned int ip, const int port);

ClusterMachine *get_machine(const unsigned int ip, const int port);

int machine_up_notify(ClusterMachine *machine);
int machine_add_connection(SocketContext *pSockContext);
int machine_remove_connection(SocketContext *pSockContext);

#ifdef __cplusplus
}
#endif

#endif

