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
#include "pow_mon.h"
#include <sys/syscall.h>


inline void get_timestamp(char * buf);
inline void my_sleep(float delay);
char const *version = "v1.0";//版本号



//获取时间戳，这里不用ns作为基准单位而是采用s，并且精确到1us
void get_timestamp(char * buf) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    sprintf(buf, "%.6f", tv.tv_sec + (tv.tv_usec / 1000000.0));
}
//提供帮助信息
void usage() {

    printf("pow_mon: 监控CPU和DRAM功耗及频率\n\n");
    printf("用法: pow_mon [-h] [-v V] PROGRAMNAME [ARGS]\n");
    printf("                     {run,start,stop,restart}\n");
    printf("  -h                    显示帮助信息并退出\n");
    printf("  -v  V                  显示版本号\n");
    printf("   PROGRAMNAME [ARGS]             需要运行的程序及参数 \n");
    exit(0);

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
    printf("\nprogram total time %uus\n",(sysd->time_us_final-sysd->time_us_init));
    
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
        sprintf(send_data,"DRAM power:%.2fW;CPU power:%.2fW;DRAM energy:%.2fJ;CPU energy:%.2fJ",drampow,pkgpow,dramerg,pkgerg);
        printf("CPU%d %s\n",cpuid,send_data);
    }

}
void main(int argc, char* argv[]) {

    FILE *fp = stderr;
    float dT = 1;
    int daemon = -1;
    char buffer[1024];
    char tmpstr[256];
    int i;
    char * token;
    struct sys_data sysd_;
    char* str_1;
    init_pow_mon(&sysd_);
    char cmdline[8192]={0};
    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-v") == 0) // daemonize
            {
                fprintf(fp, "Version: %s\n", version);
                exit(0);
            } 
            else if (strcmp(argv[i], "-h") == 0) // daemonize
            {
                usage();
                exit(0);
            } 
            else if (1) {
                strcat(cmdline, argv[i]);
                cmdline[strlen(cmdline)] = ' ';
            }                
        }
    }
    
    if (detect_cpu_model(&sysd_) < 0) {
        fprintf(fp, "Error in detecting CPU model.\n");
        exit(EXIT_FAILURE);
    }

    if (detect_topology(&sysd_) != 0) {
        fprintf(fp, "Cannot get host topology.\n");
        exit(EXIT_FAILURE);
    }

    // 给cpudata分配内存空间
    sysd_.cpu_data = (per_cpu_data *) malloc(sizeof (per_cpu_data) * sysd_.NCPU);
    // config MSR   
    program_msr(&sysd_);
    long long int time_us;
    get_timestamp(sysd_.tmpstr);
    time_us =atof(sysd_.tmpstr)*1e6;
    sysd_.time_us_init=time_us;
    init_msr_data(&sysd_);
    printf("Runing %s \n",cmdline);
    system(cmdline);
    get_timestamp(sysd_.tmpstr);
    time_us =atof(sysd_.tmpstr)*1e6;
    sysd_.time_us_final=time_us;
    final_msr_energy(&sysd_);
    show_result(&sysd_);
    cleanup_pow_mon(&sysd_);
    exit(0);

}
