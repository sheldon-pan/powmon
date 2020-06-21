#include <sys/types.h>
#include <sys/time.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include "mpi.h"

#define INITIAL_TIMER_DELAY 100000

struct monitor_t {
    int imonitor;
    int world_rank;
    int world_size;
    int node_rank;
    int node_size;
    int color;
    MPI_Comm mynode_comm;
    char my_host[MPI_MAX_PROCESSOR_NAME];
};



static int get_comm_split_color_hostname (struct monitor_t * monitor);
static void poli_sync (void);
static void poli_sync_node (void);

    


