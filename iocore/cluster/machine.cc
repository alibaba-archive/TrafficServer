#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <fcntl.h>
#include "logger.h"
#include "local_ip_func.h"
#include "pthread_func.h"
#include "global.h"
#include "nio.h"
#include "connection.h"
#include "message.h"
#include "machine.h"

#ifndef DEBUG_FLAG
#include "ink_config.h"
#include "P_Cluster.h"
#endif

unsigned int g_my_machine_index = 0;
unsigned int g_my_machine_ip = 0;
int g_my_machine_id = 0;
int g_machine_count = 0;

ClusterMachine *g_machines = NULL;  //sort by ip and port
static ClusterMachine **sorted_machines = NULL;
static pthread_mutex_t machine_lock;

static ClusterMachine *do_add_machine(ClusterMachine *m, int *result);

#ifdef DEBUG_FLAG
static int add_machine(const char *hostname, const int port)
{
  ClusterMachine machine;
  struct in_addr ip_addr;
  int result;

  memset(&machine, 0, sizeof(machine));
  machine.hostname_len = strlen(hostname);
  machine.hostname = strdup(hostname);
  machine.cluster_port = port;
  inet_pton(AF_INET, machine.hostname, &ip_addr);
  machine.ip = ip_addr.s_addr;

  do_add_machine(&machine, &result);
  return result;
}
#endif

ClusterMachine *add_machine(const unsigned int ip, const int port)
{
  ClusterMachine machine;
  struct in_addr in;
  int result;
  char *ip_addr;

  memset(&machine, 0, sizeof(machine));
  in.s_addr = ip;
  ip_addr = inet_ntoa(in);
  machine.hostname_len = strlen(ip_addr);
  machine.hostname = strdup(ip_addr);
  machine.cluster_port = port;
  machine.ip = ip;

  return do_add_machine(&machine, &result);
}

int init_machines()
{
  int result;
  int bytes;
  int i;

  if ((result=init_pthread_lock(&machine_lock)) != 0) {
    return result;
  }

  g_machine_count = 0;
  bytes = sizeof(ClusterMachine) * MAX_MACHINE_COUNT;
  g_machines = (ClusterMachine *)malloc(bytes);
  if (g_machines == NULL) {
    logError("file: "__FILE__", line: %d, "
        "malloc %d bytes fail!", __LINE__, bytes);
    return ENOMEM;
  }
  memset(g_machines, 0, bytes);

  bytes = sizeof(ClusterMachine *) * MAX_MACHINE_COUNT;
  sorted_machines = (ClusterMachine **)malloc(bytes);
  if (sorted_machines == NULL) {
    logError("file: "__FILE__", line: %d, "
        "malloc %d bytes fail!", __LINE__, bytes);
    return ENOMEM;
  }
  memset(sorted_machines, 0, bytes);

#ifdef DEBUG_FLAG
  add_machine("10.235.163.5", g_server_port);
  add_machine("10.235.163.6", g_server_port);
  /*
  add_machine("10.235.163.7", g_server_port);
  add_machine("10.235.163.8", g_server_port);
  add_machine("10.235.163.9", g_server_port);
  add_machine("10.235.163.10", g_server_port);
  */

  logInfo("ip 0: %d => %d", g_machines[0].ip, g_machines[0].ip % MAX_MACHINE_COUNT);
  logInfo("ip 1: %d => %d", g_machines[1].ip, g_machines[1].ip % MAX_MACHINE_COUNT);
  logInfo("ip 0: %d => %d", ntohl(g_machines[0].ip), ntohl(g_machines[0].ip) % MAX_MACHINE_COUNT);
  logInfo("ip 1: %d => %d", ntohl(g_machines[1].ip), ntohl(g_machines[1].ip) % MAX_MACHINE_COUNT);
#endif

  logInfo("g_machine_count: %d", g_machine_count);

  load_local_host_ip_addrs();
  //print_local_host_ip_addrs();

  for (i=0; i<g_machine_count; i++) {
    if (is_local_host_ip(g_machines[i].hostname)) {
      g_my_machine_index = i;
      g_my_machine_ip = g_machines[i].ip;
      logInfo("my_machine_id: %d", g_my_machine_id);
      break;
    }
  }

  return 0;
}

//TODO: for test only
int start_machines_connection()
{
  int i;
  for (i=0; i<g_machine_count; i++) {
    if (!is_local_host_ip(g_machines[i].hostname)) {
      logInfo("start connection: %s", g_machines[i].hostname);
      machine_make_connections(g_machines + i);
    }
  }

  return 0;
}

static int compare_machine(const void *p1, const void *p2)
{
  const ClusterMachine **m1 = (const ClusterMachine **)p1;
  const ClusterMachine **m2 = (const ClusterMachine **)p2;

  if ((*m1)->ip == (*m2)->ip) {
    return (*m1)->cluster_port - (*m2)->cluster_port;
  }
  else {
    return (*m1)->ip - (*m2)->ip;
  }
}

static ClusterMachine *do_add_machine(ClusterMachine *m, int *result)
{
  ClusterMachine **ppMachine;
  ClusterMachine **ppMachineEnd;
  ClusterMachine **pp;
  ClusterMachine *pMachine;
  int cr;

  cr = -1;
	pthread_mutex_lock(&machine_lock);
  ppMachineEnd = sorted_machines + g_machine_count;
  for (ppMachine=sorted_machines; ppMachine<ppMachineEnd; ppMachine++) {
    cr = compare_machine(&m, ppMachine);
    if (cr <= 0) {
      break;
    }
  }

  do {
    if (cr == 0) {  //found
      pMachine = *ppMachine;
      *result = EEXIST;
      break;
    }

    if (g_machine_count >= MAX_MACHINE_COUNT) {
      logError("file: "__FILE__", line: %d, "
          "exceeds max machine: %d!", __LINE__, MAX_MACHINE_COUNT);
      *result = ENOSPC;
      pMachine = NULL;
      break;
    }

    for (pp=ppMachineEnd; pp>ppMachine; pp--) {
      *pp = *(pp - 1);
    }

    pMachine = g_machines + g_machine_count;  //the last emlement
    *ppMachine = pMachine;

    pMachine->dead = true;
    pMachine->ip = m->ip;
    pMachine->cluster_port = m->cluster_port;
    pMachine->hostname_len = m->hostname_len;
    if (m->hostname_len == 0) {
      pMachine->hostname = NULL;
    }
    else {
      pMachine->hostname = (char *)malloc(m->hostname_len + 1);
      if (pMachine->hostname == NULL) {
        logError("file: "__FILE__", line: %d, "
            "malloc %d bytes fail!", __LINE__, m->hostname_len + 1);
        *result = ENOMEM;
        break;
      }
      memcpy(pMachine->hostname, m->hostname, m->hostname_len + 1);
    }

    g_machine_count++;
    *result = 0;
  } while (0);

	pthread_mutex_unlock(&machine_lock);
  return pMachine;
}

ClusterMachine *get_machine(const unsigned int ip, const int port)
{
  ClusterMachine machine;
  ClusterMachine *target;
  ClusterMachine **found;

  memset(&machine, 0, sizeof(machine));
  machine.ip = ip;
  machine.cluster_port = port;
  target = &machine;
  found = (ClusterMachine **)bsearch(&target, sorted_machines, g_machine_count,
      sizeof(ClusterMachine *), compare_machine);
  if (found != NULL) {
    return *found;
  }
  else {
    return NULL;
  }
}

int machine_up_notify(ClusterMachine *machine)
{
  if (machine == NULL) {
    return ENOENT;
  }

  pthread_mutex_lock(&machine_lock);

  logDebug("file: "__FILE__", line: %d, "
      "machine_up_notify, %s connection count: %d, dead: %d",
      __LINE__, machine->hostname, machine->now_connections, machine->dead);

  if (machine->dead) {
    machine->dead = false;
    g_machine_change_notify(machine);
  }
  pthread_mutex_unlock(&machine_lock);

  return 0;
}

int machine_add_connection(SocketContext *pSockContext)
{
  int result;
  int count;

  pthread_mutex_lock(&machine_lock);
  if ((result=nio_add_to_epoll(pSockContext)) != 0) {
    pthread_mutex_unlock(&machine_lock);
    return result;
  }

  count = ++pSockContext->machine->now_connections;
  pthread_mutex_unlock(&machine_lock);

  logDebug("file: "__FILE__", line: %d, "
      "%s add %c connection count: %d, dead: %d", __LINE__,
      pSockContext->machine->hostname, pSockContext->connect_type,
      count, pSockContext->machine->dead);

  return 0;
}

int machine_remove_connection(SocketContext *pSockContext)
{
  int count;
  int result;

  pthread_mutex_lock(&machine_lock);
  if ((result=remove_machine_sock_context(pSockContext)) != 0) {
    pthread_mutex_unlock(&machine_lock);
    return result;
  }

  count = --pSockContext->machine->now_connections;
  if (count == 0 && !pSockContext->machine->dead) { //should remove machine from config
    pSockContext->machine->dead = true;
    g_machine_change_notify(pSockContext->machine);
  }
  pthread_mutex_unlock(&machine_lock);

  logDebug("file: "__FILE__", line: %d, "
      "%s remove %c connection count: %d, dead: %d", __LINE__,
      pSockContext->machine->hostname, pSockContext->connect_type,
      count, pSockContext->machine->dead);

  return 0;
}

