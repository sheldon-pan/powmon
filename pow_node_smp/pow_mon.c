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
#include <unistd.h>
#include <sched.h>
#include "sensor_read_lib.h"
#include "pow_mon.h"
#include<stdlib.h>
 #define CMD_STR_LEN 1024

int keepRunning;
char const *version = "v1.0";//版本号


inline void push_to_broker(struct sys_data * sysd);
void sig_handler(int sig);
inline void get_timestamp(char * buf);
inline void my_sleep(float delay);
void usage();
        


//输出信息到文件或者屏幕
void push_to_broker(struct sys_data * sysd) {

    FILE* fp;
    char data[255];
    char tmp_[255];
    char tmp_s[255];
    char send_data[100000];
    char topic_tmp[255];
    int cpuid;
    int coreid;
    int i;
    long long unsigned int  tsc_diff;
    long long unsigned int  powDramC_diff;
    long long unsigned int  powPkg_diff;
    long long unsigned int  uclk_diff;
    long long unsigned int  instr_diff;
    long long unsigned int  clk_curr_diff;
    long long unsigned int  clk_ref_diff;
    long long unsigned int  aperf_diff;
    long long unsigned int  mperf_diff;
    float pkgpow;
    float drampow;
    float uclk_MHz;
    float mclk_MHz;
    float rclk_MHz; //通过clk_ref和clk_curr计算出的实时频率
    float erg_units; //MSR中记录的CPU进行rapl的最小能量单位
    float CPI;
    float IPS;
    float core_load;
    float nomfreq_MHz;
    int cpu_temp;
    int core_temp;
    
    char memtotal[100];
    char memfree[100];
    FILE * mem;
    mem=fopen("/proc/meminfo","r");
    fgets(memtotal,80,mem);
    fgets(memfree,80,mem);
    memtotal[strlen(memtotal)-1]='\0';
    memfree[strlen(memfree)-1]='\0';
    printf("%s;%s;Time:%s\n",memtotal,memfree,sysd->tmpstr);
    fclose(mem);
    for (cpuid = 0; cpuid < sysd->NCPU; cpuid++) {
        cpu_temp = sysd->cpu_data[cpuid].tempPkg;
        nomfreq_MHz =sysd->nom_freq/1000000;
        erg_units = 0.000061;
        powPkg_diff=sysd->cpu_data[cpuid].powPkg - sysd->cpu_data[cpuid].powPkg_last;
        sysd->cpu_data[cpuid].powPkg_diff = powPkg_diff;
        pkgpow = powPkg_diff * 0.000061/sysd->dT;
        sysd->cpu_data[cpuid].pkgpow=pkgpow;
        if (sysd->extra_counters == 1) {
            if (sysd->CPU_MODEL == HASWELL_EP) {
                uclk_diff= sysd->cpu_data[cpuid].uclk-sysd->cpu_data[cpuid].uclk_last;
                sysd->cpu_data[cpuid].uclk_diff = uclk_diff;
                uclk_MHz = (uclk_diff/sysd->dT)/1000000;
                sysd->cpu_data[cpuid].uclk_MHz = uclk_MHz;
               
            }         
        }
        if (sysd->DRAM_SUPP == 1) {
            powDramC_diff=sysd->cpu_data[cpuid].powDramC-sysd->cpu_data[cpuid].powDramC_last;
            sysd->cpu_data[cpuid].powDramC_diff = powDramC_diff;
            drampow = powDramC_diff * 0.0000153/sysd->dT;
            sysd->cpu_data[cpuid].drampow=drampow;
            
            
        }
        
        sprintf(topic_tmp, "%s%d","cpu",cpuid); 
        sprintf(send_data,"cpu_temp:%d;nomfreq_MHz:%.2f;uclk_MHz:%.2f;drampow:%.2f;pkgpow:%.2f;timestamp:%s",cpu_temp,nomfreq_MHz,uclk_MHz,drampow,pkgpow,sysd->tmpstr);
        printf("%s   %s\n",topic_tmp,send_data);
    }
    /*
    *每指令周期 (CPI)        
     CPI  =  clk_curr_diff/Instr.diff           
    *每秒指令数 (IPS)                
     IPS  =  Instr.diff/dt                    
    *cpu负载       
     Load  =  (clk_ref_diff/tsc_diff)*100
    *CPU频率。有两种计算方式   
    Freq  =  (clk_curr_diff/clk_ref_diff)*Freq_ref
    Freq  =  (aperf_diff/mperf_diff)*Freq_ref         
    */
  
    for (coreid = 0; coreid < sysd->NCORE; coreid++) {
        aperf_diff =sysd->core_data[coreid].aperf - sysd->core_data[coreid].aperf_last;
        sysd->core_data[coreid].aperf_diff = aperf_diff;
        mperf_diff =sysd->core_data[coreid].mperf - sysd->core_data[coreid].mperf_last;
        sysd->core_data[coreid].mperf_diff = mperf_diff;
        tsc_diff = sysd->core_data[coreid].tsc - sysd->core_data[coreid].tsc_last;
        sysd->core_data[coreid].tsc_diff = tsc_diff;
        instr_diff =sysd->core_data[coreid].instr - sysd->core_data[coreid].instr_last;
        sysd->core_data[coreid].instr_diff = instr_diff;
        clk_curr_diff =sysd->core_data[coreid].clk_curr - sysd->core_data[coreid].clk_curr_last;
        sysd->core_data[coreid].clk_curr_diff =  clk_curr_diff ;
        clk_ref_diff =sysd->core_data[coreid].clk_ref - sysd->core_data[coreid].clk_ref_last;
        sysd->core_data[coreid].clk_ref_diff =  clk_ref_diff ;
        core_temp = sysd->core_data[coreid].temp;
        CPI = (1.0*clk_curr_diff)/instr_diff;
        sysd->core_data[coreid].CPI =  CPI;
        IPS = (1.0*instr_diff)/sysd->dT;
        sysd->core_data[coreid].IPS =  IPS;
        core_load = (100.0* clk_ref_diff)/tsc_diff;
        sysd->core_data[coreid].core_load  =  core_load;
        rclk_MHz = (1.0*clk_curr_diff/clk_ref_diff)*sysd->nom_freq/1000000;
        sysd->core_data[coreid].rclk_MHz  =  rclk_MHz;
        mclk_MHz = (1.0*aperf_diff/mperf_diff)*sysd->nom_freq/1000000;
        sprintf(topic_tmp, "%s%d","core",coreid); 
        sprintf(send_data,"core_temp:%d;CPI:%.2f;IPS:%.2f;core_load:%.2f;rclk_MHz:%.2f;%s",core_temp,CPI,IPS,core_load,mclk_MHz,sysd->tmpstr);
        printf("%s    %s\n",topic_tmp,send_data);
    }
  

}



void samp_handler(struct sys_data * sysd) {
    get_timestamp(sysd->tmpstr);
    //mosquitto_publish(mosq, NULL, sysd->topic, strlen(sync_ck), sync_ck, 0, false);
    read_msr_data(sysd);
    push_to_broker(sysd);
}

void sig_handler(int sig) {

    keepRunning = 0;
    printf(" Clean exit!\n");
}

//获取时间戳，这里不用ns作为基准单位而是采用s，并且精确到1ms
void get_timestamp(char * buf) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    sprintf(buf, "%.3f", tv.tv_sec + (tv.tv_usec / 1000000.0));
}

//提供帮助信息
void usage() {

    printf("pow_csv: 本机监控CPU和DRAM功耗及频率，按照s周期进行记录\n\n");
    printf("用法: pow_csv [-h] [-s S] [-v V] \n");
    printf("  -s S                  采样间隔 (seconds)\n");
    printf("  -h                    显示帮助信息并退出\n");
    printf("  -v                    版本信息\n");

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
    memset(sysd->dieTemp, 100, sizeof (*sysd->dieTemp)); // dieTemp
    memset(sysd->dieTempEn, 0, sizeof (*sysd->dieTempEn)); // dieTempEn
    sysd->cpu_data = NULL; // cpu_data 
    sysd->core_data = NULL; // core_data 
    sysd->extra_counters = 1; 
    return 0;

}

//释放powmon的内存空间
int cleanup_pow_mon(struct sys_data * sysd) {

    free(sysd->cpu_data);
    free(sysd->core_data);

    return 0;
}






void main(int argc, char* argv[]) {

    int mosqMajor, mosqMinor, mosqRevision;
    FILE *fp = stderr;
    float dT = 5;
    int daemon = -1;
    char hostname[256];
    char pidfile[256];
    char logfile[256];
    char pidfiledir[256];
    char logfiledir[256];
    char buffer[1024];
    char* data_topic_string = "";
    char* cmd_topic_string = "";
    char tmpstr[256];
    int i;
    char delimit[] = " \t\r\n\v\f,"; //POSIX whitespace characters
    char * token;
    struct sys_data sysd_;




    init_pow_mon(&sysd_);
    fprintf(fp, "%s Version: %s\n", argv[0], version);
    fprintf(fp, "\nConf file parameters:\n\n");

    sysd_.dT=1.0;
    if (argc > 1) {
        fprintf(fp, "\nCommand line parameters (override):\n\n");
        for (i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-v") == 0) // daemonize
            {
                fprintf(fp, "Version: %s\n", version);
                exit(0);
            } else if (strcmp(argv[i], "-h") == 0) //
            {
                usage();
                exit(0);
            }
            else if (strcmp(argv[i], "-s") == 0) // sampling interval
            {
                sysd_.dT = atof(argv[i + 1]);
                fprintf(fp, "New Daemon dT: %f\n", sysd_.dT);
            } 
        }
    }
    hostname[255] = '\0';
    if (detect_cpu_model(&sysd_) < 0) {
        fprintf(fp, "[MQTT]: Error in detecting CPU model.\n");
        exit(EXIT_FAILURE);
    }

    if (detect_topology(&sysd_) != 0) {
        fprintf(fp, "[MQTT]: Cannot get host topology.\n");
        exit(EXIT_FAILURE);
    }

    if (detect_nominal_frequency(&sysd_) < 0) {
        fprintf(fp, "[MQTT]: Error in detecting Nominal Frequecy.\n");
        exit(EXIT_FAILURE);
    }

    // 给CPU、CORE和GPU 分配内存空间
    sysd_.cpu_data = (per_cpu_data *) malloc(sizeof (per_cpu_data) * sysd_.NCPU);
    sysd_.core_data = (per_core_data *) malloc(sizeof (per_core_data) * sysd_.NCORE);
    
    // config MSR
    program_msr(&sysd_);
    read_msr_data_first(&sysd_);


    if (fp != stderr)
        fclose(fp);

    signal(SIGINT, sig_handler); // Ctrl-C 退出
    signal(SIGTERM, sig_handler); // (15)
    keepRunning = 1;

    /* 进行周期间隔采样，分为自定义时间和默认的1s间隔*/

    while (keepRunning) {
        my_sleep(sysd_.dT);
        samp_handler(&sysd_);
    }
    cleanup_pow_mon(&sysd_);
    exit(0);
}
