/*
 * sensor_read_lib.h : 读取MSR能够得到一些CPU和CORE的参数，这里定义
 *一些数据结构进行保存。除了CPU。GPU的数据也可以使用类似的结构进行保存
 */
#define _GNU_SOURCE
#include <sched.h>
#ifndef SENSOR_READ_LIB_H
#define	SENSOR_READ_LIB_H
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <math.h>

#define MAX_CORES	1024
#define MAX_PACKAGES	16


/***** Intel MSR寄存器地址 ********/
/*
 * 由于PP1主要是GPU，因此这里并不进行PP0和PP1的单独读取
 * 本软件只用来记录DRAM的能量计数和PKG的能量计数
 */
#define MSR_RAPL_POWER_UNIT		0x606
/* Package RAPL Domain */
#define MSR_PKG_RAPL_POWER_LIMIT	0x610
#define MSR_PKG_ENERGY_STATUS		0x611
#define MSR_PKG_PERF_STATUS		0x613
#define MSR_PKG_POWER_INFO		0x614

/* DRAM RAPL Domain */
#define MSR_DRAM_POWER_LIMIT		0x618
#define MSR_DRAM_ENERGY_STATUS		0x619
#define MSR_DRAM_PERF_STATUS		0x61B
#define MSR_DRAM_POWER_INFO		0x61C

/* Per-Core C States */
#define MSR_CORE_C3_RESIDENCY           0x3FC
#define MSR_CORE_C6_RESIDENCY           0x3FD
#define MSR_CORE_C7_RESIDENCY           0x3FE

/* Per-Socket C States */
#define MSR_PKG_C2_RESIDENCY            0x60D
#define MSR_PKG_C3_RESIDENCY            0x3F8
#define MSR_PKG_C6_RESIDENCY            0x3F9
#define MSR_PKG_C7_RESIDENCY            0x3FA

/* RAPL UNIT BITMASK */
#define POWER_UNIT_OFFSET               0
#define POWER_UNIT_MASK                 0x0F

#define ENERGY_UNIT_OFFSET              0x08
#define ENERGY_UNIT_MASK                0x1F00

#define TIME_UNIT_OFFSET                0x10
#define TIME_UNIT_MASK                  0xF000

// 性能计数器和温度计数器
#define IA32_TEMPERATURE_TARGET         0x000001a2
#define MSR_IA32_THERM_STATUS           0x0000019c
#define MSR_IA32_PACKAGE_THERM_STATUS   0x000001b1
/* 温度计数器的蒙版 */
#define TEMP_MASK                       0x007F0000
/* Intel 性能计数器，主要用来计算实际频率 */
#define MSR_CORE_PERF_FIXED_CTR0        0x00000309
#define MSR_CORE_PERF_FIXED_CTR1        0x0000030a
#define MSR_CORE_PERF_FIXED_CTR2        0x0000030b
#define MSR_CORE_PERF_FIXED_CTR_CTRL    0x0000038d
#define MSR_CORE_PERF_GLOBAL_STATUS     0x0000038e
#define MSR_CORE_PERF_GLOBAL_CTRL       0x0000038f
#define MSR_CORE_PERF_GLOBAL_OVF_CTRL   0x00000390




/* CPU性能模型Aperf 和Mperf  */
#define MSR_MPERF                       0xe7
#define MSR_APERF                       0xe8
/* CPU的次代信息地址  */
#define PLATFORM_INFO_ADDR              0xce

/* UBox performance monitoring MSR */
#define U_MSR_PMON_UCLK_FIXED_CTL       0x0703      //counter 64bit
#define U_MSR_PMON_UCLK_FIXED_CTR       0x0704      //control 48bit

#define DECLARE_ARGS(val, low, high)  unsigned low, high
#define EAX_EDX_VAL(val, low, high) ((low) | ((uint64_t)(high) << 32))
#define EAX_EDX_RET(val, low, high) "=a" (low), "=d" (high)


/* Intel CPU Codenames */
#define SANDYBRIDGE	42
#define SANDYBRIDGE_EP  45
#define IVYBRIDGE	58
#define IVYBRIDGE_EP	62
#define HASWELL		60	
#define HASWELL_EP	63
#define BROADWELL	61	
#define BROADWELL_EP	79
#define BROADWELL_DE	86
#define SKYLAKE		78
#define SKYLAKE_HS	94



/* data structures */
typedef struct {
    int ergU ;
    int ergDram;
    unsigned int powPkg_last;
    unsigned int powPkg ;
    unsigned int powPkg_init ;
    unsigned int powPkg_final ;
    unsigned int powPkg_overflow ;
    unsigned int tempPkg ;
    unsigned int powDramC_last;
    unsigned int powDramC ;
    unsigned int powDramC_init ;
    unsigned int powDramC_final ;
    unsigned int powDramC_overflow ;
}per_cpu_data;

struct sys_data {
    int NCPU;
    int NCORE;
    int CPU_MODEL;
    int HT_EN;
    float nom_freq;
    int DRAM_SUPP;
    per_cpu_data *cpu_data;
    float dT;
    char tmpstr[80];
    long long int time_us;
    long long int time_us_init;
    long long int time_us_final;
};


int open_msr(int core);
long long read_msr(int fd, int which);
void write_msr(int fd, int which, uint64_t data);
unsigned long long read_tsc(void);
inline void init_msr_data(struct sys_data * sysd);
inline void read_msr_energy (struct sys_data * sysd);
inline void final_msr_energy (struct sys_data * sysd);
int detect_topology(struct sys_data * sysd);
int detect_cpu_model(struct sys_data * sysd);
int detect_nominal_frequency(struct sys_data * sysd);
int program_msr(struct sys_data * sysd);
inline int set_cpu_affinity(unsigned int cpu);
inline void canc_cpu_affinity(struct sys_data * sysd);



#endif /* SENSOR_READ_LIB_H */
