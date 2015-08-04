/*
 * Energy and performance measurement utility functions
 *
 * Author: Mikael Hirki <mikael.hirki@aalto.fi>
 */

#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>

#define millisleep(x)	(usleep((x) * 1000))

/* Assuming that RAND_MAX is 2^31 - 1 */
#define rand64()	(((unsigned long long)rand() << 62) | ((unsigned long long)rand() << 31) | rand())
#define rand32()	(((unsigned int)rand() << 31) | rand())

#if __x86_64__ || __i386__
#define HAVE_RDTSC
#define RDTSC(v)							\
  do { unsigned lo, hi;							\
    __asm__ volatile("rdtsc" : "=a" (lo), "=d" (hi));			\
    (v) = ((uint64_t) lo) | ((uint64_t) hi << 32);			\
  } while (0)
#else
#define RDTSC(v) (v = 0)
#endif

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

/* PAPI gives energy in nanojoules */
#define ENERGY_SCALE_FACTOR	(1e-9)

/* Flags for measure_init_v2 and measure_stop_v2 */
#define MEASURE_FLAG_NO_PRINT	0x01
#define MEASURE_FLAG_NO_ENERGY	0x02

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	/* PAPI event sets */
	int papi_energy_events;
	int papi_perf_events;
	
	/* Number of events in each set */
	int num_energy_events;
	int num_perf_events;
	
	/* Nanosecond timing information */
	struct timespec begin_time;
	struct timespec end_time;
	
	/* TSC timing information */
	unsigned long long begin_tsc;
	unsigned long long end_tsc;
	
	/* Core temperatures */
	double begin_temp_pkg;
	double begin_temp0;
	double begin_temp1;
	double begin_temp2;
	double begin_temp3;
	double end_temp_pkg;
	double end_temp0;
	double end_temp1;
	double end_temp2;
	double end_temp3;
	
	/* Core voltages */
	double begin_voltage0;
	double begin_voltage1;
	double begin_voltage2;
	double begin_voltage3;
	double end_voltage0;
	double end_voltage1;
	double end_voltage2;
	double end_voltage3;
	
	/* Buffers for PAPI_read() */
	long long *papi_energy_values;
	long long *papi_perf_values;
	
	/* For storing computed RAPL power consumption */
	double pkg_power_before;
	double pp0_power_before;
	double pp1_power_before;
	double dram_power_before;
	double time_elapsed_before;
	double event_1_before;
	double event_2_before;
	double event_3_before;
	double event_4_before;
	
	/* Indices for PAPI event sets */
	int idx_pkg_energy;
	int idx_pp0_energy;
	int idx_pp1_energy;
	int idx_dram_energy;
	int idx_cycles;
	int idx_ref_cycles;
	int idx_instructions;
	int idx_event_1;
	int idx_event_2;
	int idx_event_3;
	int idx_event_4;
	
	/* Flags */
	char energy_started;
	char perf_started;
	char have_rapl;
} measure_state_t;

/*
 * Some PAPI functions don't seem to be thread safe...
 */
extern pthread_mutex_t papi_mutex;

/*
 * Cache event codes for faster performance.
 */
extern int papi_code_uops_issued;
extern int papi_code_idq_mite;
extern int papi_code_idq_dsb;
extern int papi_code_idq_ms;

int measure_init_papi(int flags);
int measure_init_thread(measure_state_t *state, int flags);
int measure_start(measure_state_t *s, int flags);
int measure_stop(measure_state_t *state, int flags);
int measure_combine_perf_results(measure_state_t *this, measure_state_t *other);
int measure_print(measure_state_t *state, int flags);
int measure_cleanup(measure_state_t *state);
void *measure_alloc(size_t size);
void *measure_aligned_alloc(size_t size, size_t alignment);

/*
 * New higher level interface
 */

typedef struct {
	const char *name;
	const char *desc;
} perf_counter_t;

typedef struct {
	int (*init)(void **benchdata);
	int (*normal)(void *benchdata, long ntimes);
	int (*extreme)(void *benchdata, long ntimes);
	int (*cleanup)(void *benchdata);
	perf_counter_t counters[4]; /* Not yet implemented */
	long ntimes;
} measure_benchmark_t;

/*
 * Parsed command line parameters
 */
extern char arg_do_measure;
extern char arg_use_64bit_numbers;
extern int  arg_benchmark_phase;
extern int  arg_num_threads;
extern int  arg_num_repeat;
extern int  arg_warmup_time;
extern char arg_force_affinity;

int measure_main(int argc, char **argv, measure_benchmark_t *bench);

#ifdef __cplusplus
} /* extern "C" */
#endif
