/*
 * 用读取MSR的方式，来进行CPU实时频率以及功耗的测量
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>
#include <inttypes.h>
#include <sched.h>
#include "sensor_read_lib.h"
#include "powmpi.h"
#include <sys/syscall.h>
#include <time.h>
#include <math.h>
#include <mpi.h>


struct monitor_t *monitor = 0;
char const *version = "v1.0";//版本号
struct sys_data sysd_;
//获取时间戳，这里不用ns作为基准单位而是采用s，并且精确到1us

void get_timestamp(char * buf) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    sprintf(buf, "%.6f", tv.tv_sec + (tv.tv_usec / 1000000.0));
}


inline void my_sleep(float delay) {
    struct timespec sleep_intrval;
    struct timeval tp;
    double now;

    gettimeofday(&tp, NULL);
    now = (double) tp.tv_sec + tp.tv_usec * 1e-6;
    delay -= fmod(now, delay);

    sleep_intrval.tv_nsec = (delay - (int) delay)*1e9;
    sleep_intrval.tv_sec = (int) delay;
    //printf("%lld.%.9ld\n", (long long)sleep_intrval.tv_sec, sleep_intrval.tv_nsec);     
    nanosleep(&sleep_intrval, NULL);

}
//初始化powmon的数据结构
int init_pow_mon(struct sys_data * sysd) {

    memset(sysd, 0, sizeof (*sysd));
    sysd->NCPU = 0; // NCPU;
    sysd->NCORE = 0; // NCORE;
    sysd->CPU_MODEL = -1; // CPU_MODEL;
    sysd->cpu_data = NULL; // cpu_data 
    sysd->dT = 1.0; // dT;
    return 0;

}

//释放powmon的内存空间
int cleanup_pow_mon(struct sys_data * sysd) {
    free(sysd->cpu_data);
    return 0;
}


//显示程序运行过程中能量统计的结果，以及指令数，CPI，IPS等信息
void show_result(struct sys_data * sysd){
    FILE* fp;
    char data[255];
    char send_data[100000];
    char topic_tmp[255];
    int cpuid;
    int i;
    long long int powDramC_diff;
    long long int powPkg_diff;
    float pkgpow;
    float drampow;
    float pkgerg;
    float dramerg;
    long long int time_us;
    get_timestamp(sysd_.tmpstr);
    time_us =atof(sysd_.tmpstr)*1e6;
    sysd_.time_us_final=time_us;
    printf("Rank%d on Node COMPUTE-1-%d begin:%fs end:%fs total mpi wtime %fs\n", \
    monitor->world_rank,monitor->color,sysd->initial_mpi_wtime,  \
    MPI_Wtime(),(MPI_Wtime()-sysd->initial_mpi_wtime));
    printf("Rank%d on Node COMPUTE-1-%d begin:%fs end:%fs total us wtime %uus\n", \
    monitor->world_rank,monitor->color,sysd->time_us_final,  \
    sysd->time_us_init,sysd->time_us_final-sysd->time_us_init);
   // printf("program total time %uus\n",(sysd->time_us_final-sysd->time_us_init));
   // printf("program total mpi wtime %fs\n",);
    if ((MPI_Wtime()-sysd->initial_mpi_wtime<0.002)|(sysd->time_us_final-sysd->time_us_init)<2000){
        printf("Program ran too short,MSR sampling need 1ms or longer\n");
        exit(1);
    }
    
    for (cpuid = 0; cpuid < sysd->NCPU; cpuid++) {
        powPkg_diff=sysd->cpu_data[cpuid].powPkg_final - sysd->cpu_data[cpuid].powPkg_init;
        //PKG的平均功率
        pkgerg = powPkg_diff * pow(0.5,sysd->cpu_data[cpuid].ergU);
        pkgpow=  pkgerg/(sysd->time_us_final-sysd->time_us_init)*1e6;
        
        powDramC_diff=sysd->cpu_data[cpuid].powDramC_final-sysd->cpu_data[cpuid].powDramC_init;
        if (sysd->DRAM_SUPP==1|sysd->DRAM_SUPP==2)
        {
        dramerg = powDramC_diff * pow(0.5,sysd->cpu_data[cpuid].ergDram);
        drampow = dramerg /(sysd->time_us_final-sysd->time_us_init)*1e6;  
        }
        else{
            drampow = 0;
            dramerg = 0;
        }          
        //sprintf(send_data,"DRAM power:%.4fW;CPU power:%.4fW;DRAM energy:%.4fJ;CPU energy:%.4fJ",drampow,pkgpow,dramerg,pkgerg);
        //printf("CPU%d %s\n",cpuid,send_data);
        printf("Rank%d on Node COMPUTE-1-%d CPU%d DRAM power:%.4fW;CPU power:%.4fW;DRAM energy:%.4fJ;CPU energy:%.4fJ\n",   \
       monitor->world_rank,monitor->color,cpuid,drampow,pkgpow,dramerg,pkgerg);
    }

}

//将COMPUTE-1-* 相同的*设为color，因此可以划分为同一个通信子域
static int get_comm_split_color_hostname (struct monitor_t * monitor)
{
    char hostname[MPI_MAX_PROCESSOR_NAME];
    int resultlen;
    //获取当前rank的域名
    MPI_Get_processor_name(hostname, &resultlen);
    char *token;
    token = strtok(hostname, " \t.\n");
    strcpy(monitor->my_host, token);
    //使用域名的前缀COPMPUTE-1- 进行字符串分割，如COMPUTE-1-46，分割出来就是46
    char *hostnum = strtok(monitor->my_host, "-");
    strtok(NULL, "-");
    hostnum = strtok(NULL, "-");
    monitor->color = atoi(hostnum);
    return 0;
}
static void poli_sync (void)
{

    int finalized;
    //MPI结束为1，未结束为0
    MPI_Finalized(&finalized);
    if (monitor->world_size > 1 && !finalized)
        MPI_Barrier(MPI_COMM_WORLD);

    return;
}

static void poli_sync_node (void)
{

    int finalized;
    MPI_Finalized(&finalized);
    if (monitor->node_size > 1 && !finalized)
        MPI_Barrier(monitor->mynode_comm);

    return;
}

int poli_init (void)
{    
    monitor = malloc(sizeof(struct monitor_t));
    monitor->imonitor = 0;
    // get current MPI environment
    MPI_Comm_size(MPI_COMM_WORLD, &monitor->world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &monitor->world_rank);
    memset(monitor->my_host, '\0', sizeof(monitor->my_host));
    get_comm_split_color_hostname(monitor);
    //划分MPI通信子域，相同hostname的rank被并为同一通信子域，因为所有的域名包含COMPUTE-1- 因此使用时划分COMPUTE-1-* 相同的*为同一color
    MPI_Comm_split(MPI_COMM_WORLD, monitor->color, monitor->world_rank, &monitor->mynode_comm);
    MPI_Comm_size(monitor->mynode_comm, &monitor->node_size);
    MPI_Comm_rank(monitor->mynode_comm, &monitor->node_rank);
    //设置通信子域，也就是同一节点的唯一测量进程
    if (monitor->node_rank == 0 )
        monitor->imonitor = 1;
    if (monitor->imonitor)
    {
        init_pow_mon(&sysd_);
        if (detect_cpu_model(&sysd_) < 0) {
            printf("Error in detecting CPU model.\n");
            exit(EXIT_FAILURE);
        }
        if (detect_topology(&sysd_) != 0) {
            printf("Cannot get host topology.\n");
            exit(EXIT_FAILURE);
        }
         //初始化墙钟时间
       
        // 给cpudata分配内存空间
        sysd_.cpu_data = (per_cpu_data *) malloc(sizeof (per_cpu_data) * sysd_.NCPU);

        // config MSR   
        program_msr(&sysd_);
        long long int time_us;
        get_timestamp(sysd_.tmpstr);
        time_us =atof(sysd_.tmpstr)*1e6;
        sysd_.time_us_init=time_us;
        sysd_.initial_mpi_wtime = MPI_Wtime();
        init_msr_data(&sysd_);
    }

    poli_sync();

    return 0;
}

int poli_finalize(void)
{
    poli_sync();
    if (monitor->imonitor)
    {  

        final_msr_energy(&sysd_);
        show_result(&sysd_);
        cleanup_pow_mon(&sysd_);       
    }
    int finalized;
    MPI_Finalized(&finalized);
    if (!finalized)
        MPI_Comm_free(&monitor->mynode_comm);
    if (monitor)
        free(monitor);
    return 0;
}

int MPI_Init(int *argc, char ***argv){     
    int rank, err;     
    err = PMPI_Init(argc, argv);    
    poli_init();
    return err; 
    
}   

int MPI_Finalize(){ 	
    int rank, err; 	
    
    poli_finalize();
    return PMPI_Finalize();  
}