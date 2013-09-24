//machine.h

#ifndef _MACHINE_H
#define _MACHINE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned int my_machine_ip;
extern int cluster_machine_count;
extern struct ClusterMachine *cluster_machines;

int init_machines();
ClusterMachine *add_machine(const unsigned int ip, const int port);

ClusterMachine *get_machine(const unsigned int ip, const int port);

int machine_up_notify(ClusterMachine *machine);
int machine_add_connection(SocketContext *pSockContext);
int machine_remove_connection(SocketContext *pSockContext);

#ifdef __cplusplus
}
#endif

#endif

