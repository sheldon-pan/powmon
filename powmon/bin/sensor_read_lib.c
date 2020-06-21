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
#include "pow_mon.h"

//判断MSR是否可读
int open_msr(int core) {

  char msr_filename[BUFSIZ];
  int fd;

  sprintf(msr_filename, "/dev/cpu/%d/msr", core);
  fd = open(msr_filename, O_RDWR);
  if ( fd < 0 ) {
    if ( errno == ENXIO ) {
      fprintf(stderr, "rdmsr: No CPU %d\n", core);
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



//读取MSR信息 填入数据结构
inline void read_msr_data(struct sys_data * sysd){

    int cpuid = 0;
    int core = 0;
    int i;
    int fd;
    uint64_t tsc;
    uint64_t result;
    unsigned int mask;

    for (core=0;core<sysd->NCORE;core++){
        set_cpu_affinity(core);
        fd=open_msr(core);
        tsc = read_tsc();
        if ((core==0)|(core==sysd->NCORE/2)){
            cpuid = trunc(core*sysd->NCPU)/sysd->NCORE;
            sysd->cpu_data[cpuid].tsc = tsc;
            if (sysd->dieTempEn[cpuid] == 0){
                result           = read_msr(fd,IA32_TEMPERATURE_TARGET);
                sysd->dieTemp[cpuid]   = (result >> 16) & 0x0ff;
                sysd->dieTempEn[cpuid] = 1;
            }
            sysd->cpu_data[cpuid].ergU      = read_msr(fd,MSR_RAPL_POWER_UNIT);
            sysd->cpu_data[cpuid].powPkg_last = sysd->cpu_data[cpuid].powPkg;
            sysd->cpu_data[cpuid].powPkg    = read_msr(fd,MSR_PKG_ENERGY_STATUS);
            result                          = read_msr(fd,MSR_IA32_PACKAGE_THERM_STATUS);
            sysd->cpu_data[cpuid].tempPkg   = sysd->dieTemp[cpuid] - ((result & TEMP_MASK ) >> 16);
                       
            if (sysd->DRAM_SUPP==1){
                sysd->cpu_data[cpuid].powDramC_last = sysd->cpu_data[cpuid].powDramC;
                sysd->cpu_data[cpuid].powDramC = read_msr(fd,MSR_DRAM_ENERGY_STATUS);
            }
            else
                sysd->cpu_data[cpuid].powDramC = 0;

            // PKG的其余参数包括C3 C6计数以及haswell的uncore频率。uncore频率其实应该也是处于可调的
            if (sysd->extra_counters == 1){
                sysd->cpu_data[cpuid].C2_last = sysd->cpu_data[cpuid].C2;
                sysd->cpu_data[cpuid].C2        = read_msr(fd,MSR_PKG_C2_RESIDENCY);
                sysd->cpu_data[cpuid].C3_last = sysd->cpu_data[cpuid].C3;
                sysd->cpu_data[cpuid].C3        = read_msr(fd,MSR_PKG_C3_RESIDENCY);
                sysd->cpu_data[cpuid].C6_last = sysd->cpu_data[cpuid].C6;
                sysd->cpu_data[cpuid].C6        = read_msr(fd,MSR_PKG_C6_RESIDENCY);
                if(sysd->CPU_MODEL==HASWELL_EP){
                    sysd->cpu_data[cpuid].uclk_last = sysd->cpu_data[cpuid].uclk;
                    sysd->cpu_data[cpuid].uclk = read_msr(fd,U_MSR_PMON_UCLK_FIXED_CTR);  
                } 
            }
            
        }
        result = read_msr(fd,MSR_CORE_PERF_FIXED_CTR_CTRL);
        mask   = 0x0333;
        result = result | mask;
        write_msr(fd,MSR_CORE_PERF_FIXED_CTR_CTRL,result);
        sysd->core_data[core].tsc_last = sysd->core_data[core].tsc ;

        sysd->core_data[core].tsc       = tsc;
        result                          = read_msr(fd,MSR_IA32_THERM_STATUS);
        sysd->core_data[core].temp      = sysd->dieTemp[cpuid] - ((result & TEMP_MASK ) >> 16);  
        sysd->core_data[core].instr_last   = sysd->core_data[core].instr;
        sysd->core_data[core].instr     = read_msr(fd,MSR_CORE_PERF_FIXED_CTR0);
        sysd->core_data[core].clk_curr_last  = sysd->core_data[core].clk_curr;
        sysd->core_data[core].clk_curr  = read_msr(fd,MSR_CORE_PERF_FIXED_CTR1);
        sysd->core_data[core].clk_ref_last  = sysd->core_data[core].clk_ref;
        sysd->core_data[core].clk_ref   = read_msr(fd,MSR_CORE_PERF_FIXED_CTR2);
        sysd->core_data[core].C3_last  = sysd->core_data[core].C3;
        sysd->core_data[core].C3        = read_msr(fd,MSR_CORE_C3_RESIDENCY);
        sysd->core_data[core].C6_last  = sysd->core_data[core].C6;
        sysd->core_data[core].C6        = read_msr(fd,MSR_CORE_C6_RESIDENCY);
        sysd->core_data[core].aperf_last  = sysd->core_data[core].aperf;
        sysd->core_data[core].mperf_last = sysd->core_data[core].mperf;
        sysd->core_data[core].aperf     = read_msr(fd,MSR_APERF);
        sysd->core_data[core].mperf     = read_msr(fd,MSR_MPERF);

        close(fd);
    }

}

//初始化msr记录
inline void read_msr_data_first(struct sys_data * sysd){

    int cpuid = 0;
    int core = 0;
    int i;
    int fd;
    uint64_t tsc;
    uint64_t result;
    unsigned int mask;

    for (core=0;core<sysd->NCORE;core++){
        set_cpu_affinity(core);
        fd=open_msr(core);
        tsc = read_tsc();
        if ((core==0)|(core==sysd->NCORE/2)){
            cpuid = trunc(core*sysd->NCPU)/sysd->NCORE;
            sysd->cpu_data[cpuid].tsc = tsc;
            if (sysd->dieTempEn[cpuid] == 0){
                result           = read_msr(fd,IA32_TEMPERATURE_TARGET);
                sysd->dieTemp[cpuid]   = (result >> 16) & 0x0ff;
                sysd->dieTempEn[cpuid] = 1;
            }
            sysd->cpu_data[cpuid].ergU      = read_msr(fd,MSR_RAPL_POWER_UNIT);
            sysd->cpu_data[cpuid].powPkg_last = sysd->cpu_data[cpuid].powPkg;
            sysd->cpu_data[cpuid].powPkg    = read_msr(fd,MSR_PKG_ENERGY_STATUS);
            result                          = read_msr(fd,MSR_IA32_PACKAGE_THERM_STATUS);
            sysd->cpu_data[cpuid].tempPkg   = sysd->dieTemp[cpuid] - ((result & TEMP_MASK ) >> 16);
                       
            if (sysd->DRAM_SUPP==1){
                sysd->cpu_data[cpuid].powDramC_last = sysd->cpu_data[cpuid].powDramC;
                sysd->cpu_data[cpuid].powDramC = read_msr(fd,MSR_DRAM_ENERGY_STATUS);
            }
            else
                sysd->cpu_data[cpuid].powDramC = 0;

            // PKG的其余参数包括C3 C6计数以及haswell的uncore频率。uncore频率其实应该也是处于可调的
            if (sysd->extra_counters == 1){
                sysd->cpu_data[cpuid].C2_last = sysd->cpu_data[cpuid].C2;
                sysd->cpu_data[cpuid].C2        = read_msr(fd,MSR_PKG_C2_RESIDENCY);
                sysd->cpu_data[cpuid].C3_last = sysd->cpu_data[cpuid].C3;
                sysd->cpu_data[cpuid].C3        = read_msr(fd,MSR_PKG_C3_RESIDENCY);
                sysd->cpu_data[cpuid].C6_last = sysd->cpu_data[cpuid].C6;
                sysd->cpu_data[cpuid].C6        = read_msr(fd,MSR_PKG_C6_RESIDENCY);
                if(sysd->CPU_MODEL==HASWELL_EP){
                    sysd->cpu_data[cpuid].uclk_last = sysd->cpu_data[cpuid].uclk;
                    sysd->cpu_data[cpuid].uclk = read_msr(fd,U_MSR_PMON_UCLK_FIXED_CTR);  
                } 
            }
            
        }
        result = read_msr(fd,MSR_CORE_PERF_FIXED_CTR_CTRL);
        mask   = 0x0333;
        result = result | mask;
        write_msr(fd,MSR_CORE_PERF_FIXED_CTR_CTRL,result);
        sysd->core_data[core].tsc_last = sysd->core_data[core].tsc ;
        
        sysd->core_data[core].tsc       = tsc;
        result                          = read_msr(fd,MSR_IA32_THERM_STATUS);
        sysd->core_data[core].temp      = sysd->dieTemp[cpuid] - ((result & TEMP_MASK ) >> 16);  
        sysd->core_data[core].instr_last   = sysd->core_data[core].instr;
        sysd->core_data[core].instr     = read_msr(fd,MSR_CORE_PERF_FIXED_CTR0);
        sysd->core_data[core].clk_curr_last  = sysd->core_data[core].clk_curr;
        sysd->core_data[core].clk_curr  = read_msr(fd,MSR_CORE_PERF_FIXED_CTR1);
        sysd->core_data[core].clk_ref_last  = sysd->core_data[core].clk_ref;
        sysd->core_data[core].clk_ref   = read_msr(fd,MSR_CORE_PERF_FIXED_CTR2);
        sysd->core_data[core].C3_last  = sysd->core_data[core].C3;
        sysd->core_data[core].C3        = read_msr(fd,MSR_CORE_C3_RESIDENCY);
        sysd->core_data[core].C6_last  = sysd->core_data[core].C6;
        sysd->core_data[core].C6        = read_msr(fd,MSR_CORE_C6_RESIDENCY);
        sysd->core_data[core].aperf_last  = sysd->core_data[core].aperf;
        sysd->core_data[core].mperf_last = sysd->core_data[core].mperf;
        sysd->core_data[core].aperf     = read_msr(fd,MSR_APERF);
        sysd->core_data[core].mperf     = read_msr(fd,MSR_MPERF);

        close(fd);
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

    printf("\nDetecting host topology...\n\n");

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
        printf("Hyperthreading enabled\n");
        sysd->HT_EN = 1;
    } else {
        printf("Hyperthreading disabled\n");
        sysd->HT_EN = 0;
    }

    printf("%d physical sockets\n", total_sockets);
    printf("%d cores per socket\n", cores_per_socket);
    printf("%d total cores\n", (cores_per_socket * total_sockets));
    printf("%d logical CPUs\n", total_logical_cpus);

    sysd->NCORE = (cores_per_socket * total_sockets);
    sysd->NCPU = total_sockets;


    return 0;

}
//判断CPU的型号。通过读取/proc/cpuinfo文件进行判断
int detect_cpu_model(struct sys_data * sysd) {

    FILE *fd;
    int model = -1;
    char buffer[BUFSIZ];
    char *result;

    printf("\nDetecting CPU model...\n\n");

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
                (model == HASWELL_EP) ||
                (model == HASWELL) ||
                (model == BROADWELL)) {

            sysd->DRAM_SUPP = 1;
        }
    }

    return model;
}
//读取CPU的tsc时间戳
inline unsigned long long read_tsc(void)
{
  DECLARE_ARGS(val, low, high);

  asm volatile("rdtsc" : EAX_EDX_RET(val, low, high));

  return EAX_EDX_VAL(val, low, high);
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

//设置当前正在采样的CPU序号
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



