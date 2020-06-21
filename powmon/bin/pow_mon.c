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
#include <time.h>
#include <math.h>
#include <inttypes.h>
#include <unistd.h>
#include <sched.h>
#include "mosquitto.h"
#include "iniparser.h"
#include "sensor_read_lib.h"
#include "pow_mon.h"
#include<stdlib.h>

struct mosquitto* mosq;
timer_t timer1;
int keepRunning;
char * sync_ck = "";//同步的信号
char const *version = "v1.0";//版本号


inline void push_to_broker(struct sys_data * sysd, struct mosquitto * mosq);
void sig_handler(int sig);
int start_timer(struct sys_data * sysd);
inline void get_timestamp(char * buf);
void daemonize(char * pidfile);
int daemon_stop(char * pidfile);
int daemon_status(char * pidfile);
inline void my_sleep(float delay);
int enabled_host(char * host, char * host_whitelist_file, struct sys_data * sysd);
char **strsplit(const char* str, const char* delim, int* numtokens);
void usage();


#ifdef USE_TIMER  
//这里定义的就是抽样函数，通过判断是否用户自定义抽样时间，来进行不同抽样函数的定义

void samp_handler(int signo, siginfo_t *si, void *uc) {
    struct sys_data *sysd;
    sysd = (struct sys_data *) si->si_value.sival_ptr;
#else

void samp_handler(struct sys_data * sysd) {
#endif
    get_timestamp(sysd->tmpstr);
    //mosquitto_publish(mosq, NULL, sysd->topic, strlen(sync_ck), sync_ck, 0, false);
    read_msr_data(sysd);
    push_to_broker(sysd, mosq);
}

/* mqtt连接状态判断 */
void on_connect_callback(struct mosquitto *mosq, void *obj, int result) {
    struct sys_data *sysd;
    assert(obj);
    sysd = (struct sys_data *) obj;
    fprintf(stderr, "[MQTT]: Subscribing to command topic...\n");
    if (!result) {
        mosquitto_subscribe(mosq, NULL, sysd->cmd_topic, 0); // QoS!
        fprintf(stderr, "[MQTT]: Ready!\n");
    } else {
        fprintf(stderr, "%s\n", mosquitto_connack_string(result));
    }
}

/* MQTT服务器的信息 */
void on_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message) {

    char * data = NULL;
    struct sys_data *sysd;
    char brokerHost[256];
   
    int brokerPort;
    int i;
    char buffer[BUFSIZ];
    float dt;
    char delimit[] = " \t\r\n\v\f,"; //POSIX whitespace characters
    char * tmpstr = NULL;

    assert(obj);
    sysd = (struct sys_data *) obj;

    fprintf(stderr, "[MQTT]: cmd received\n");
    if (message->payloadlen) {
        /* print data */
        data = (char *) (message->payload); // get payload

        // parse commands
        if (!strncmp(data, "-s", 2)) {
            sscanf(data, "%*s%f", &sysd->dT);
            fprintf(stderr, "New dT: %f\n", sysd->dT);
#ifdef USE_TIMER
            timer_delete(timer1);
            start_timer(sysd);
#endif  
        }

        if (!strncmp(data, "-b", 2)) {
            sscanf(data, "%*s%s%d", brokerHost, &brokerPort);
            fprintf(stderr, "New brokerHost: %s\n", brokerHost);
            fprintf(stderr, "New brokerPort: %d\n", brokerPort);

            if (mosquitto_disconnect(mosq) != MOSQ_ERR_SUCCESS) {
                fprintf(stderr, "\n [MQTT]: Error while disconnecting!\n");
            }
            sleep(1);
            if (mosquitto_connect(mosq, brokerHost, brokerPort, 1000) != MOSQ_ERR_SUCCESS) {
                fprintf(stderr, "\n [MQTT]: Could not connect to broker\n");
                mosquitto_connect(mosq, sysd->brokerHost, sysd->brokerPort, 1000);
            }
            mosquitto_loop_start(mosq);
        }

        if (!strncmp(data, "-t", 2)) {
            sscanf(data, "%*s%s", buffer);
            sysd->topic = strdup(buffer);
            fprintf(stderr, "New topic: %s\n", sysd->topic);
        }

    }
}
int diff_count(uint64_t count_new,uint64_t count_last){
    long long int result;
    result = count_new -count_last;
    return result;
}

//这里定义的是向broker进行消息的推送
void push_to_broker(struct sys_data * sysd, struct mosquitto * mosq) {

    FILE* fp;
    char data[255];
    char tmp_[255];
    char tmp_s[255];
    char send_data[100000];
    char topic_tmp[255];
    int cpuid;
    int coreid;
    int i;
    long long unsigned int tsc_diff;
    long long unsigned int C2_diff;
    long long unsigned int C3_diff;
    long long unsigned int C6_diff;
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
    float mclk_MHz; //通过aperf和mperf计算出的实时频率
    float rclk_MHz; //通过clk_ref和clk_curr计算出的实时频率
    float erg_units; //MSR中记录的CPU进行rapl的最小能量单位
    float CPI;
    float IPS;
    float core_load;
    float nomfreq_MHz;
    int cpu_temp;
    int core_temp;
 
    fp = fopen(sysd->logfile, "a");

    for (cpuid = 0; cpuid < sysd->NCPU; cpuid++) {
        cpu_temp = sysd->cpu_data[cpuid].tempPkg;
        //PUB_METRIC("cpu", "cpu_temp",cpu_temp , cpuid, "%u;%s");
        nomfreq_MHz =sysd->nom_freq/1000000;
        //PUB_METRIC("cpu", "nomfreq_MHz", nomfreq_MHz, cpuid, "%.2f;%s");
        erg_units = 0.000061;
        powPkg_diff=sysd->cpu_data[cpuid].powPkg - sysd->cpu_data[cpuid].powPkg_last;
        sysd->cpu_data[cpuid].powPkg_diff = powPkg_diff;
        pkgpow = powPkg_diff * 0.000061/sysd->dT;
        sysd->cpu_data[cpuid].pkgpow=pkgpow;
        if (sysd->extra_counters == 1) {
            C2_diff =sysd->cpu_data[cpuid].C2-sysd->cpu_data[cpuid].C2_last;
            sysd->cpu_data[cpuid].C2_diff = C2_diff;
            C3_diff =sysd->cpu_data[cpuid].C3-sysd->cpu_data[cpuid].C3_last;
            sysd->cpu_data[cpuid].C3_diff = C3_diff;
            C6_diff =sysd->cpu_data[cpuid].C6-sysd->cpu_data[cpuid].C6_last;
            sysd->cpu_data[cpuid].C6_diff = C6_diff;
            if (sysd->CPU_MODEL == HASWELL_EP) {
                uclk_diff= sysd->cpu_data[cpuid].uclk-sysd->cpu_data[cpuid].uclk_last;
                sysd->cpu_data[cpuid].uclk_diff = uclk_diff;
                uclk_MHz = (uclk_diff/sysd->dT)/1000000;
                sysd->cpu_data[cpuid].uclk_MHz = uclk_MHz;
                //PUB_METRIC("cpu", "uclk_MHz", uclk_MHz, cpuid, "%.2f;%s");
            }         
        }
        if (sysd->DRAM_SUPP == 1) {
            powDramC_diff=sysd->cpu_data[cpuid].powDramC-sysd->cpu_data[cpuid].powDramC_last;
            sysd->cpu_data[cpuid].powDramC_diff = powDramC_diff;
            drampow = powDramC_diff * 0.0000153/sysd->dT;
            sysd->cpu_data[cpuid].drampow=drampow;
             //PUB_METRIC("cpu","pow_dram", drampow, cpuid, "%.2f;%s");
            
        }
        //PUB_METRIC("cpu","pow_pkg", pkgpow, cpuid, "%.2f;%s");
        sprintf(topic_tmp, "%s/%s%d", sysd->topic,"cpu",cpuid); 
        sprintf(send_data,"%u;%.2f;%.2f;%.2f;%.2f;%s",cpu_temp,nomfreq_MHz,uclk_MHz,drampow,pkgpow,sysd->tmpstr);
        if(mosquitto_publish(mosq, NULL, topic_tmp, strlen(send_data), send_data, sysd->qos, false) != MOSQ_ERR_SUCCESS) { \
        fprintf(fp, "[MQTT]: Warning: cannot send message.\n");  \
        } \
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
        tsc_diff = sysd->core_data[coreid].tsc - sysd->core_data[coreid].tsc_last;
        sysd->core_data[coreid].tsc_diff = tsc_diff;
        if (sysd->extra_counters == 1) {
            C3_diff =sysd->core_data[coreid].C3 - sysd->core_data[coreid].C3_last;
            sysd->core_data[coreid].C3_diff = C3_diff;
            C6_diff =sysd->core_data[coreid].C6 - sysd->core_data[coreid].C6_last;
            sysd->core_data[coreid].C3_diff = C3_diff;
            aperf_diff =sysd->core_data[coreid].aperf - sysd->core_data[coreid].aperf_last;
            sysd->core_data[coreid].aperf_diff = aperf_diff;
            mperf_diff =sysd->core_data[coreid].mperf - sysd->core_data[coreid].mperf_last;
            sysd->core_data[coreid].mperf_diff = mperf_diff;
        }
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
        mclk_MHz = (1.0*aperf_diff/mperf_diff)*sysd->nom_freq/1000000;
        sysd->core_data[coreid].mclk_MHz  =  mclk_MHz;
        rclk_MHz = (1.0*clk_curr_diff/clk_ref_diff)*sysd->nom_freq/1000000;
        sysd->core_data[coreid].rclk_MHz  =  rclk_MHz;
        //PUB_METRIC("core", "CPI", CPI, coreid, "%.2f;%s"); 
        //PUB_METRIC("core", "IPS", IPS, coreid, "%.2f;%s"); 
        //PUB_METRIC("core", "Load", core_load, coreid, "%.2f;%s");   
        //PUB_METRIC("core", "mclk_MHz", mclk_MHz, coreid, "%.2f;%s");
        //PUB_METRIC("core", "rclk_MHz", rclk_MHz, coreid, "%.2f;%s");
        sprintf(topic_tmp, "%s/%s%d", sysd->topic,"core",coreid); 
        sprintf(send_data,"%d;%.2f;%.2f;%.2f;%.2f;%.2f;%s",core_temp,CPI,IPS,core_load,mclk_MHz,rclk_MHz,sysd->tmpstr);
        if(mosquitto_publish(mosq, NULL, topic_tmp, strlen(send_data), send_data, sysd->qos, false) != MOSQ_ERR_SUCCESS) { \
        fprintf(fp, "[MQTT]: Warning: cannot send message.\n");  \
        } \
    }
   

    fclose(fp);
}

void sig_handler(int sig) {

#ifdef USE_TIMER
    timer_delete(timer1);
#endif
    keepRunning = 0;
    printf(" Clean exit!\n");
}
进行定时，需要每隔1小时进行一次rdate的时间同步
int start_timer(struct sys_data * sysd) {

    struct itimerspec new_value, old_value;
    struct sigaction action;
    struct sigevent sevent;
    sigset_t set;
    int signum;
    float dT = 0;

    memset(&action, 0, sizeof (struct sigaction));
    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = samp_handler;
    if (sigaction(SIGRTMAX, &action, NULL) == -1)
        perror("sigaction");


    memset(&sevent, 0, sizeof (sevent));
    sevent.sigev_notify = SIGEV_SIGNAL;
    sevent.sigev_signo = SIGRTMAX;
    sevent.sigev_value.sival_ptr = sysd;

    dT = sysd->dT;

    if (timer_create(CLOCK_MONOTONIC, &sevent, &timer1) == 0) {

        new_value.it_interval.tv_sec = (int) dT;
        new_value.it_interval.tv_nsec = (dT - (int) dT)*1000000000;
        new_value.it_value.tv_sec = (int) dT;
        new_value.it_value.tv_nsec = (dT - (int) dT)*1000000000;

        my_sleep(dT); //align

        if (timer_settime(timer1, 0, &new_value, &old_value) != 0) {
            perror("timer_settime");
            return 1;
        }

    } else {
        perror("timer_create");
        return 1;
    }
    return 0;

}
//获取时间戳，这里不用ns作为基准单位而是采用s，并且精确到1ms
void get_timestamp(char * buf) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    sprintf(buf, "%.3f", tv.tv_sec + (tv.tv_usec / 1000000.0));
}
//后台进程化，提供进程号
void daemonize(char * pidfile) {

    pid_t process_id = 0;
    pid_t sid = 0;
    FILE *fp = NULL;

    process_id = fork();
    if (process_id < 0) {
        printf("fork failed!\n");
        exit(1);
    }
    if (process_id > 0) {
        printf("process_id of child process %d \n", process_id);
        fp = fopen(pidfile, "w");
        fprintf(fp, "%d\n", process_id);
        fclose(fp);
        exit(0);
    }
    umask(0);
    sid = setsid();
    if (sid < 0) {
        exit(1);
    }
    //chdir("/");
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

}
//进程停止
int daemon_stop(char * pidfile) {

    FILE* fp;
    char cmd[100];
    char pid[100];
    pid_t pid_;
    int ret = 0;


    printf("open file %s!\n", pidfile);
    fp = fopen(pidfile, "r");
    if (fp != NULL) {
        fscanf(fp, "%s", pid);
        printf("Process pid = %s!\n", pid);
        pid_ = atoi(pid);
        ret = 1;
        fclose(fp);
    }
    if (daemon_status(pidfile)) {
        ret = 1;
    } else {
        printf("Daemon is not running!\n");
        ret = 0;
    }
    if (ret == 1) {
        printf("killing pid: %d!\n", pid_);
        kill(pid_, SIGINT);
        sleep(1);
    }

    return ret;
}
//进程状态
int daemon_status(char * pidfile) {

    FILE* fp;
    FILE* fd;
    struct stat sts;
    char cmd[100];
    char pid[100];
    char name[100];
    int ret = 0;

    fp = fopen(pidfile, "r");
    if (fp != NULL) {
        fscanf(fp, "%s", pid);
        sprintf(cmd, "/proc/%s/comm", pid);
        fd = fopen(cmd, "r");
        if (fd != NULL) {
            fscanf(fd, "%s", name);
            if (strncmp(name, "pow_mon", 7) != 0) {
                printf("Process does not exist!\n");
                ret = 0;
            } else {
                printf("Daemon is running!\n");
                ret = 1;
            }
            fclose(fd);
        } else {
            printf("Process does not exist!\n");
            ret = 0;
        }
        fclose(fp);
    } else {
        printf("Daemon is not running!\n");
        ret = 0;
    }
    return ret;
}

//提供帮助信息
void usage() {

    printf("pow_mon: 监控CPU和DRAM功耗及频率\n\n");
    printf("用法: pow_mon [-h] [-b B] [-p P] [-t T] [-q Q] [-s S] [-x X]\n");
    printf("                     [-l L] [-e E] [-c C] [-P P] [-v] \n");
    printf("                     {run,start,stop,restart}\n");
    printf("可选参数，如果不设置，默认使用conf文件:\n");
    printf("  -h                    显示帮助信息并退出\n");
    printf("  -b B                  MQTT broker的IP地址\n");
    printf("  -p P                  MQTT broker端口号\n");
    printf("  -s S                  采样间隔 (seconds)\n");
    printf("  -t T                  传给Broker的Topic\n");
    printf("  -q Q                  Qos级别 (0：无确认,1：一次握手,2：两次握手)\n");
    printf("  -x X                  Pid进程信息目录\n");
    printf("  -l L                  Log日志文件目录\n");
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
    strcpy(sysd->logfile, ""); // logfile
    strcpy(sysd->tmpstr, ""); // tmpstr
    sysd->hostid = NULL; // hostid;
    sysd->topic = NULL; // topic;
    sysd->cmd_topic = NULL; // cmd_topic;
    sysd->brokerHost = NULL; // brokerHost;
    sysd->brokerPort = 1883; // brokerPort;
    sysd->qos = 0; // qos;
    sysd->dT = 1.0; // dT;
    sysd->extra_counters = 1; 
    return 0;

}

//释放powmon的内存空间
int cleanup_pow_mon(struct sys_data * sysd) {

    free(sysd->cpu_data);
    free(sysd->core_data);

    return 0;
}

char **strsplit(const char* str, const char* delim, int* numtokens) {

    char *s = strdup(str);
    int tokens_alloc = 1;
    int tokens_used = 0;
    char **tokens = calloc(tokens_alloc, sizeof (char*));
    char *token, *strtok_ctx;


    for (token = strtok_r(s, delim, &strtok_ctx);
            token != NULL;
            token = strtok_r(NULL, delim, &strtok_ctx)) {
        // check if we need to allocate more space for tokens
        if (tokens_used == tokens_alloc) {
            tokens_alloc *= 2;
            tokens = realloc(tokens, tokens_alloc * sizeof (char*));
        }
        tokens[tokens_used++] = strdup(token);
    }
    // cleanup
    if (tokens_used == 0) {
        free(tokens);
        tokens = NULL;
    } else {
        tokens = realloc(tokens, tokens_used * sizeof (char*));
    }
    *numtokens = tokens_used;
    free(s);
    return tokens;
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
    char* conffile = "pow_mon.conf";
    char* host_whitelist_file = "host_whitelist";
    char* data_topic_string = "";
    char* cmd_topic_string = "";
    char tmpstr[256];
    int i;
    dictionary *ini;
    char delimit[] = " \t\r\n\v\f,"; //POSIX whitespace characters
    char * token;
    struct sys_data sysd_;
    int rdate_cycle; //每3000个时间周期进行一次时间同步
    char* str_1;



    init_pow_mon(&sysd_);

    if (argc == 1)
        fprintf(fp, "Using configuration in file: %s\n", conffile);
    ini = iniparser_load(conffile);
    if (ini == NULL) { // search in /etc/
        strcpy(tmpstr, "/etc/");
        strcat(tmpstr, conffile);
        ini = iniparser_load(tmpstr);
        if (ini == NULL) {
            fprintf(fp, "Cannot parse file: %s\n", conffile);
            usage();
        }
    }

    fprintf(fp, "%s Version: %s\n", argv[0], version);
    fprintf(fp, "\nConf file parameters:\n\n");
    iniparser_dump(ini, stderr);

    sysd_.brokerHost = iniparser_getstring(ini, "MQTT:brokerHost", NULL);
    sysd_.brokerPort = iniparser_getint(ini, "MQTT:brokerPort", 1883);
    sysd_.topic = iniparser_getstring(ini, "MQTT:topic", NULL);
    sysd_.cmd_topic = iniparser_getstring(ini, "MQTT:cmd_topic", NULL);
    sysd_.qos = iniparser_getint(ini, "MQTT:qos", 0);
    sysd_.dT = iniparser_getdouble(ini, "Daemon:dT", 1);
    daemon = iniparser_getboolean(ini, "Daemon:daemonize", 0);
    strcpy(pidfiledir, iniparser_getstring(ini, "Daemon:pidfilename", "./"));
    strcpy(logfiledir, iniparser_getstring(ini, "Daemon:logfilename", "./"));
    sysd_.hostid = iniparser_getstring(ini, "Daemon:hostid", "node");

    if (argc > 1) {
        fprintf(fp, "\nCommand line parameters (override):\n\n");
        for (i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-p") == 0) // broker port
            {
                sysd_.brokerPort = atoi(argv[i + 1]);
                fprintf(fp, "New broker port: %d\n", sysd_.brokerPort);
            } else if (strcmp(argv[i], "-t") == 0) // topic name
            {
                sysd_.topic = strdup(argv[i + 1]);
                fprintf(fp, "New topic name: %s\n", sysd_.topic);
            } else if (strcmp(argv[i], "-i") == 0) // cmd topic name
            {
                sysd_.cmd_topic = strdup(argv[i + 1]);
                fprintf(fp, "New cmd topic name: %s\n", sysd_.cmd_topic);
            } else if (strcmp(argv[i], "-b") == 0) // broker ip address
            {
                sysd_.brokerHost = strdup(argv[i + 1]);
                fprintf(fp, "New brokerhost: %s\n", sysd_.brokerHost);
            } else if (strcmp(argv[i], "-q") == 0) // QOS
            {
                sysd_.qos = atoi(argv[i + 1]);
                fprintf(fp, "New QoS: %d\n", sysd_.qos);
            } else if (strcmp(argv[i], "-s") == 0) // sampling interval
            {
                sysd_.dT = atof(argv[i + 1]);
                fprintf(fp, "New Daemon dT: %f\n", sysd_.dT);
            } else if (strcmp(argv[i], "-n") == 0) // unique hostid
            {
                sysd_.hostid = strdup(argv[i + 1]);
                fprintf(fp, "New hostid: %s\n", sysd_.hostid);
            } else if (strcmp(argv[i], "-x") == 0) // pidfiledir
            {
                strcpy(pidfiledir, argv[i + 1]);
                fprintf(fp, "New pidfiledir: %s\n", pidfiledir);
            } else if (strcmp(argv[i], "-l") == 0) // logfiledir
            {
                strcpy(logfiledir, argv[i + 1]);
                fprintf(fp, "New logfile: %s\n", logfiledir);
            } else if (strcmp(argv[i], "-h") == 0) // help
            {
                usage();
            } else if (strcmp(argv[i], "-v") == 0) // daemonize
            {
                fprintf(fp, "Version: %s\n", version);
                exit(0);
            } else if (strcmp(argv[i], "start") == 0) // daemonize
            {
                daemon = START;
            } else if (strcmp(argv[i], "run") == 0) // normal execution (no daemon)
            {
                daemon = RUN;
            } else if (strcmp(argv[i], "stop") == 0) // daemon stop
            {
                daemon = STOP;
            } else if (strcmp(argv[i], "status") == 0) // daemon status
            {
                daemon = STATUS;
            } else if (strcmp(argv[i], "restart") == 0) // daemon restart
            {
                daemon = RESTART;
            }
        }
    }

    if (gethostname(hostname, 255) != 0) {
        fprintf(fp, "[MQTT]: Cannot get hostname.\n");
        exit(EXIT_FAILURE);
    }
    hostname[255] = '\0';
    printf("Hostname: %s\n", hostname);

    sprintf(pidfile, "%s%s_%s", pidfiledir, hostname, "pow_mon.pid");
    sprintf(sysd_.logfile, "%s%s_%s", logfiledir, hostname, "pow_mon.log");


    sprintf(buffer, "%s/%s/%s", sysd_.topic, hostname, cmd_topic_string);
    sysd_.cmd_topic = strdup(buffer);
    fprintf(fp, "Cmd topic name: %s\n", sysd_.cmd_topic);
    sprintf(buffer, "%s/%s", sysd_.topic, hostname);
    sysd_.topic = strdup(buffer);
    fprintf(fp, "Data topic name: %s\n", sysd_.topic);

    str_1 = "rdate -s   ";
    char* rdate_cmd = (char*)malloc(strlen(str_1) + strlen(sysd_.brokerHost));
    sprintf(rdate_cmd,"%s%s",str_1,sysd_.brokerHost);
    fprintf(fp, "%s\n",rdate_cmd);
    system(rdate_cmd);
    fprintf(fp, "与broker时间同步完毕\n");//在初始化的时候进行时间同步
    switch (daemon) {
        case START:
            if (daemon_status(pidfile)) {
                fprintf(fp, "Exiting...\n");
                exit(0);
            }
            fprintf(fp, "Start now...\n");
            fprintf(fp, "Daemon mode...\n");
            fprintf(fp, "Open log file: %s\n", sysd_.logfile);
            fp = fopen(sysd_.logfile, "w");
            daemonize(pidfile);
            break;
        case STOP:
            daemon_stop(pidfile);
            exit(0);
            break;
        case RUN:
            if (daemon_status(pidfile)) {
                fprintf(fp, "Exiting...\n");
                exit(0);
            }
            fprintf(fp, "Start now...\n");
            break;
        case STATUS:
            daemon_status(pidfile);
            exit(0);
            break;
        case RESTART:
            if (daemon_status(pidfile) == 0) {
                fprintf(fp, "Exiting...\n");
                exit(0);
            }
            daemon_stop(pidfile);
            fprintf(fp, "Restart now...\n");
            fprintf(fp, "Daemon mode...\n");
            fprintf(fp, "Open log file: %s\n", sysd_.logfile);
            fp = fopen(sysd_.logfile, "w");
            daemonize(pidfile);
            break;
        default:
            fprintf(fp, "Exiting...\n");
            exit(0);
            break;
    }


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
    // MQTT信息
    mosquitto_lib_version(&mosqMajor, &mosqMinor, &mosqRevision);
    fprintf(fp, "[MQTT]: Mosquitto Library Version %d.%d.%d\n", mosqMajor, mosqMinor, mosqRevision);
    mosquitto_lib_init();

    //mosq来定义mqtt的连接，可以使用该链接直接进行发送，或者是使用python来处理得到的消息
    mosq = mosquitto_new(NULL, true, &sysd_);
    if (!mosq) {
        perror(NULL);
        exit(EXIT_FAILURE);
    }

    mosquitto_connect_callback_set(mosq, on_connect_callback);
    mosquitto_message_callback_set(mosq, on_message_callback);


    fprintf(fp, "[MQTT]: Connecting to broker %s on port %d\n", sysd_.brokerHost, sysd_.brokerPort);
    while (mosquitto_connect(mosq, sysd_.brokerHost, sysd_.brokerPort, 1000) != MOSQ_ERR_SUCCESS) {
        fprintf(fp, "\n [MQTT]: Could not connect to broker\n");
        fprintf(fp, "\n [MQTT]: Retry in 60 seconds...\n");
        sleep(60);
    }
    if (fp != stderr)
        fclose(fp);


    mosquitto_loop_start(mosq);

    signal(SIGINT, sig_handler); // Ctrl-C 退出
    signal(SIGTERM, sig_handler); // (15)
    keepRunning = 1;
    rdate_cycle =0;

    /* 进行周期间隔采样，分为自定义时间和默认的1s间隔*/
#ifdef USE_TIMER
    start_timer(&sysd_);
    while (keepRunning) {

        pause();

    }
#else //在conf中的采样间隔，默认1s
    while (keepRunning) {

        rdate_cycle +=1;
        if(rdate_cycle==3000){
        system(rdate_cmd);
        fprintf(fp, "与broker时间同步完毕\n");//之后每3000个时钟周期进行一次时间同步
        rdate_cycle=0;
        }
        
        my_sleep(sysd_.dT);
        samp_handler(&sysd_);
    }
#endif
    
    
    fp = fopen(sysd_.logfile, "a");
    fprintf(fp, "\n [MQTT]: exiting loop... \n");
    fprintf(fp, "\n [MQTT]: Disconnecting from broker... \n");
    if (mosquitto_disconnect(mosq) != MOSQ_ERR_SUCCESS) {
        fprintf(fp, "\n [MQTT]: Error while disconnecting!\n");
        exit(EXIT_FAILURE);
    }
    fclose(fp);
    mosquitto_destroy(mosq);
    iniparser_freedict(ini);
    cleanup_pow_mon(&sysd_);
    exit(0);

}
