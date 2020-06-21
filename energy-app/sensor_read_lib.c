#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <sched.h>
#include "sensor_read_lib.h"

//判断MSR是否可读
int open_msr(int core) {

  char msr_filename[BUFSIZ];
  int fd;

  sprintf(msr_filename, "/dev/cpu/%d/msr", core);
  fd = open(msr_filename, O_RDWR);
  if ( fd < 0 ) {
    if ( errno == ENXIO ) {
      fprintf(stderr, "rdmsr: msr_safe not available %d\n", core);
      exit(2);
    } else if ( errno == EIO ) {
      fprintf(stderr, "rdmsr: CPU %d doesn't support MSRs\n", core);
      exit(3);
    } else {
      perror("rdmsr:open");
      fprintf(stderr,"Trying to open %s\n",msr_filename);
      exit(127);
    }
  }

  return fd;
}
//MSR读取函数，读取数据为64位无符号数
long long read_msr(int fd, int which) {
  uint64_t data;
  if ( pread(fd, &data, sizeof data, which) != sizeof data ) {
    perror("rdmsr:pread");
    exit(127);
  }
  return (long long)data;
}
//MSR写入函数
void write_msr(int fd, int which, uint64_t data) {
  if ( pwrite(fd, &data, sizeof data, which) != sizeof data ) {
    perror("wrmsr:pwrite");
    exit(127);
  }
}

inline void read_msr_energy (struct sys_data * sysd)
{
    int cpuid = 0;
    int core = 0;
    int i;
    int fd;
    uint64_t tsc;
    uint64_t result;
    unsigned int mask;
    unsigned int powDramC_overflow;
    unsigned int powPkg_overflow;
    powPkg_overflow=0;
    powDramC_overflow=0;

    for (core=0;core<sysd->NCORE;core++){
        if ((core==0)|(core==sysd->NCORE/2)){
            set_cpu_affinity(core);
            fd=open_msr(core);
            cpuid = trunc(core*sysd->NCPU)/sysd->NCORE;
            sysd->cpu_data[cpuid].powPkg_last = sysd->cpu_data[cpuid].powPkg;
            sysd->cpu_data[cpuid].powPkg    = read_msr(fd,MSR_PKG_ENERGY_STATUS);  
            if(sysd->cpu_data[cpuid].powPkg_last >  sysd->cpu_data[cpuid].powPkg){
                sysd->cpu_data[cpuid].powPkg_overflow++;
            } 
            if (sysd->DRAM_SUPP==1){
                sysd->cpu_data[cpuid].powDramC_last = sysd->cpu_data[cpuid].powDramC;
                sysd->cpu_data[cpuid].powDramC = read_msr(fd,MSR_DRAM_ENERGY_STATUS);
                if(sysd->cpu_data[cpuid].powDramC_last > sysd->cpu_data[cpuid].powDramC)
                sysd->cpu_data[cpuid].powDramC_overflow++;
            }
            if (sysd->DRAM_SUPP==2){
                sysd->cpu_data[cpuid].powDramC_last = sysd->cpu_data[cpuid].powDramC;               
                sysd->cpu_data[cpuid].powDramC = read_msr(fd,MSR_DRAM_ENERGY_STATUS);
                if(sysd->cpu_data[cpuid].powDramC_last > sysd->cpu_data[cpuid].powDramC)
                sysd->cpu_data[cpuid].powDramC_overflow++;
            }
            else
                sysd->cpu_data[cpuid].powDramC = 0;
            canc_cpu_affinity(sysd);
        }
         
    }
}
inline void final_msr_energy (struct sys_data * sysd)
{
    int cpuid = 0;
    int core = 0;
    int i;
    int fd;
    uint64_t tsc;
    uint64_t result;
    unsigned int mask;

    for (core=0;core<sysd->NCORE;core++){
        if ((core==0)|(core==sysd->NCORE/2)){
            set_cpu_affinity(core);
            fd=open_msr(core);
            cpuid = trunc(core*sysd->NCPU)/sysd->NCORE;
            sysd->cpu_data[cpuid].powPkg_last = sysd->cpu_data[cpuid].powPkg;
            sysd->cpu_data[cpuid].powPkg    = read_msr(fd,MSR_PKG_ENERGY_STATUS);  
            sysd->cpu_data[cpuid].powPkg_final    = sysd->cpu_data[cpuid].powPkg;   
            if(sysd->cpu_data[cpuid].powPkg_last >sysd->cpu_data[cpuid].powPkg) {
                sysd->cpu_data[cpuid].powPkg_overflow++;
            } 
            if (sysd->DRAM_SUPP==1){
                sysd->cpu_data[cpuid].powDramC_last = sysd->cpu_data[cpuid].powDramC;
                sysd->cpu_data[cpuid].powDramC = read_msr(fd,MSR_DRAM_ENERGY_STATUS);
                sysd->cpu_data[cpuid].powDramC_final = sysd->cpu_data[cpuid].powDramC;
                if(sysd->cpu_data[cpuid].powDramC_last > sysd->cpu_data[cpuid].powDramC){
                    sysd->cpu_data[cpuid].powDramC_overflow++;
                }

            }
            if (sysd->DRAM_SUPP==2){
                sysd->cpu_data[cpuid].powDramC_last = sysd->cpu_data[cpuid].powDramC;               
                sysd->cpu_data[cpuid].powDramC = read_msr(fd,MSR_DRAM_ENERGY_STATUS);
                sysd->cpu_data[cpuid].powDramC_final = sysd->cpu_data[cpuid].powDramC;
                if(sysd->cpu_data[cpuid].powDramC_last > sysd->cpu_data[cpuid].powDramC){
                    sysd->cpu_data[cpuid].powDramC_overflow++;
                }              
            }
            else{
                sysd->cpu_data[cpuid].powDramC = 0;
                sysd->cpu_data[cpuid].powDramC_final = sysd->cpu_data[cpuid].powDramC;
                }
            canc_cpu_affinity(sysd); 
        }     
    }
}

inline void init_msr_data(struct sys_data * sysd){

    int cpuid = 0;
    int core = 0;
    int i;
    int fd;
    uint64_t tsc;
    uint64_t result;
    unsigned int mask;
    
    for (core=0;core<sysd->NCORE;core++){
        if ((core==0)|(core==sysd->NCORE/2)){
            set_cpu_affinity(core);
            fd=open_msr(core);
            cpuid = trunc(core*sysd->NCPU)/sysd->NCORE;
            sysd->cpu_data[cpuid].powPkg_overflow=0;
            sysd->cpu_data[cpuid].powDramC_overflow=0;
            sysd->cpu_data[cpuid].powDramC=0;
            sysd->cpu_data[cpuid].powPkg=0;
            sysd->cpu_data[cpuid].ergU           =  read_msr(fd,MSR_RAPL_POWER_UNIT)>>8&0x1E;
            sysd->cpu_data[cpuid].powPkg_last    =  sysd->cpu_data[cpuid].powPkg;
            sysd->cpu_data[cpuid].powPkg         =  read_msr(fd,MSR_PKG_ENERGY_STATUS);
            sysd->cpu_data[cpuid].powPkg_init    =  sysd->cpu_data[cpuid].powPkg ;       
            if (sysd->DRAM_SUPP==1){
                sysd->cpu_data[cpuid].powDramC_last = sysd->cpu_data[cpuid].powDramC;
                sysd->cpu_data[cpuid].powDramC      = read_msr(fd,MSR_DRAM_ENERGY_STATUS);
                sysd->cpu_data[cpuid].powDramC_init = sysd->cpu_data[cpuid].powDramC;
                sysd->cpu_data[cpuid].ergDram =  sysd->cpu_data[cpuid].ergU;
                
            }
            if (sysd->DRAM_SUPP==2){
                sysd->cpu_data[cpuid].powDramC_last = sysd->cpu_data[cpuid].powDramC;               
                sysd->cpu_data[cpuid].powDramC = read_msr(fd,MSR_DRAM_ENERGY_STATUS);
                sysd->cpu_data[cpuid].powDramC_init = sysd->cpu_data[cpuid].powDramC;
                sysd->cpu_data[cpuid].ergDram =  16;
            }
            else
                sysd->cpu_data[cpuid].powDramC = 0;
            canc_cpu_affinity(sysd);
         
        }
    }

}


//判断所有CPU的拓扑结构，通过读取/proc/cpuinfo文件进行判断
int detect_topology(struct sys_data * sysd) {

    FILE *fd;
    int total_logical_cpus = 0;
    int total_sockets = 0;
    int cores_per_socket = 0;
    int logical_cpus[MAX_CORES];
    int sockets[MAX_PACKAGES];
    int cores[MAX_CORES];
    int hyperthreading = 0;
    int cpu = 0;
    int phys_id = 0;
    int core = 0;
    int i;
    char buffer[BUFSIZ];
    char *result;
    fd = fopen("/proc/cpuinfo", "r");
    if (fd == NULL) {
        printf("Cannot parse file: /proc/cpuinfo\n");
        return -1;
    }
    for (i = 0; i < MAX_CORES; i++) logical_cpus[i] = -1;
    for (i = 0; i < MAX_PACKAGES; i++) sockets[i] = -1;
    for (i = 0; i < MAX_CORES; i++) cores[i] = -1;
    while (1) {
        result = fgets(buffer, BUFSIZ, fd);
        if (result == NULL) break;

        if (!strncmp(result, "processor", 9)) {
            sscanf(result, "%*s%*s%d", &cpu);

            if (logical_cpus[cpu] == -1) {
                logical_cpus[cpu] = 1;
                total_logical_cpus += 1;
            }
        }
        if (!strncmp(result, "physical id", 11)) {
            sscanf(result, "%*s%*s%*s%d", &phys_id);

            if (sockets[phys_id] == -1) {
                sockets[phys_id] = 1;
                total_sockets += 1;
            }
        }
        if (!strncmp(result, "core id", 7)) {
            sscanf(result, "%*s%*s%*s%d", &core);

            if (cores[core] == -1) {
                cores[core] = 1;
                cores_per_socket += 1;
            }
        }
    }

    fclose(fd);

    if ((cores_per_socket * total_sockets) * 2 == total_logical_cpus)
        hyperthreading = 1;

    if (hyperthreading) {
        sysd->HT_EN = 1;
    } else {
        sysd->HT_EN = 0;
    }
    sysd->NCORE = (cores_per_socket * total_sockets);
    sysd->NCPU = total_sockets;
    printf("%d physical sockets\n", total_sockets);
    printf("%d cores per socket\n", cores_per_socket);
    return 0;
   

}
//判断CPU的型号。通过读取/proc/cpuinfo文件进行判断
int detect_cpu_model(struct sys_data * sysd) {

    FILE *fd;
    int model = -1;
    char buffer[BUFSIZ];
    char *result;
    fd = fopen("/proc/cpuinfo", "r");
    if (fd == NULL) {
        printf("Cannot parse file: /proc/cpuinfo\n");
        exit(1);
    }

    while (1) {
        result = fgets(buffer, BUFSIZ, fd);
        if (result == NULL) break;
        if (!strncmp(result, "model", 5)) {
            sscanf(result, "%*s%*s%d", &model);
        }
    }

    fclose(fd);
    printf("CPU type: ");
    switch (model) {
        case SANDYBRIDGE:
            printf("Sandybridge");
            break;
        case SANDYBRIDGE_EP:
            printf("Sandybridge-EP");
            break;
        case IVYBRIDGE:
            printf("Ivybridge");
            break;
        case IVYBRIDGE_EP:
            printf("Ivybridge-EP");
            break;
        case HASWELL:
            printf("Haswell");
            break;
        case HASWELL_EP:
            printf("Haswell-EP");
            break;
        case BROADWELL:
            printf("Broadwell");
            break;
        case BROADWELL_EP:
            printf("Broadwell_EP");
            break;
        case BROADWELL_DE:
            printf("Broadwell_DE");
            break; 
        case SKYLAKE:
            printf("Skylake");
            break;
        case SKYLAKE_HS:
            printf("Skylake_HS");
            break;
        default:
            printf("Unknown model %d\n", model);
            model = -1;
            break;
    }

    printf("\n");

    if (model > 0) {
        sysd->CPU_MODEL = model;
        // DRAM power reading support    
        if ((model == SANDYBRIDGE_EP) ||
                (model == IVYBRIDGE_EP) ||
                (model == HASWELL) ||
                (model == BROADWELL)) {

            sysd->DRAM_SUPP = 1;
        }
        if (model == HASWELL_EP)
        sysd->DRAM_SUPP = 2;
    }

    return model;
}

//检测CPU的基准频率，这里使用读取MSR来判断CPU型号，然后提供型号对应的总线基频100或者133MHz*默认最大倍频
int detect_nominal_frequency(struct sys_data * sysd) {

    uint64_t result;
    uint64_t bus_freq;
    uint64_t nom_freq = 0;
    int model;
    int fd;

    printf("\nDetecting CPU Nominal Frequency...\n\n");

    if (sysd->CPU_MODEL > 0) {
        model = sysd->CPU_MODEL;
    } else {
        printf("\n Error, CPU model Unknown! \n");
        return -1;
    }


    if ((model == SANDYBRIDGE) ||
            (model == SANDYBRIDGE_EP) ||
            (model == IVYBRIDGE) ||
            (model == IVYBRIDGE_EP) ||
            (model == HASWELL) ||
            (model == HASWELL_EP) ||
            (model == BROADWELL) ||
            (model == BROADWELL_EP) ||
            (model == BROADWELL_DE) ||
            (model == SKYLAKE_HS)) {

        bus_freq = 100000000;

    } else {

        bus_freq = 133333333;

    }

    fd = open_msr(0);
    result = read_msr(fd, PLATFORM_INFO_ADDR);
    close(fd);

    // 最大非睿频比例Maximum Non-Turbo Ratio (R/O) - 15:8 bitfield
    nom_freq = ((result >> 0x8) & 0xff) * bus_freq;

    if (!nom_freq) {
        printf("\nError in detecting CPU Nominal Frequency! \n");
        return -1;
    } else {
        printf("\nCPU Nominal Frequency: %f MHz \n\n", (float) nom_freq / 1000000);
        sysd->nom_freq = nom_freq;
    }

    return 0;

}




//设置当前正在采样的Core序号，设定core亲和性
inline int set_cpu_affinity(unsigned int cpu) {

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    if (sched_setaffinity(getpid(), sizeof (cpu_set_t), &cpuset) < 0) {
        perror("sched_setaffinity");
        fprintf(stderr, "warning: unable to set cpu affinity\n");
        return -1;
    }
    return 0;
}

//取消core亲和性
inline void canc_cpu_affinity(struct sys_data * sysd){
    cpu_set_t cpuset;
    int core;
    for (core=0;core<sysd->NCORE;core++){
    CPU_SET(core, &cpuset);
    }
    sched_setaffinity(getpid(), sizeof (cpu_set_t), &cpuset);
}


//设置unclock的固定计数位
int program_msr(struct sys_data * sysd) {

    uint64_t result, mask;
    int core, cpuid;
    int fd;

    for (core = 0; core < sysd->NCORE; core++) {
        set_cpu_affinity(core);
        fd = open_msr(core);
        // Per CPU
        if ((core == 0) | (core == sysd->NCORE / 2)) {
            cpuid = trunc(core * sysd->NCPU) / sysd->NCORE;
            // Enable uncore clock
            if (sysd->CPU_MODEL == HASWELL_EP) {
                result = read_msr(fd, U_MSR_PMON_UCLK_FIXED_CTL);
                mask = 0x400000;
                result |= mask;
                write_msr(fd, U_MSR_PMON_UCLK_FIXED_CTL, result);
            }
        }
    }

    return 0;
}



