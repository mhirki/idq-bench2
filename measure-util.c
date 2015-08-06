/*
 * Energy and performance measurement utility functions
 *
 * Based on RAPL code originally written by Filip Nybäck.
 *
 * My hardware/software setup:
 * CPU: Intel Core i7-4770 @ 3.4 GHz
 * Memory: 2 x 8 GB DDR3 1600 MHz
 * OS: Scientific Linux 6.6
 * Compiler: gcc version 4.4.7 20120313 (Red Hat 4.4.7-11)
 * Compiler optimizations: -O2
 *
 * Author: Mikael Hirki <mikael.hirki@aalto.fi>
 */

/* Needed for setting CPU affinity */
#define _GNU_SOURCE

#include "measure-util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>

#include <papi.h>

/* MSR_PERF_STATUS contains core voltage */
#define MSR_PERF_STATUS		0x0198

/* MSR_IA32_THERM_STATUS contains the core temperature */
#define MSR_IA32_THERM_STATUS		0x019c

/* MSR_IA32_TEMPERATURE_TARGET contains the critical temperate (tjmax) */
#define MSR_IA32_TEMPERATURE_TARGET	0x01a2

/* MSR_IA32_PACKAGE_THERM_STATUS contains the package temperate */
#define MSR_IA32_PACKAGE_THERM_STATUS		0x01b1

/* Default value for critical temperate is 100 degrees C */
static int tjmax = 100;

/* File descriptors for MSR special files in /dev/cpu */
static int core0_fd = -1;
static int core1_fd = -1;
static int core2_fd = -1;
static int core3_fd = -1;

/* Running as root is required for RAPL, temperature and voltage information */
static char running_as_root = 1;

/* Number of CPUs available */
static int cpus_available = 1;

static void measure_warmup(measure_state_t *state);

/*
 * Event names used by libpfm4.
 */
const char *perf_event_1_name = "UOPS_ISSUED:ANY";
const char *perf_event_2_name = "IDQ:MITE_UOPS";
const char *perf_event_3_name = "IDQ:DSB_UOPS";
const char *perf_event_4_name = "IDQ:MS_UOPS";

/*
 * Human-friendly names for the events.
 */
const char *perf_event_1_pretty_name = "Uops issued:";
const char *perf_event_2_pretty_name = "MITE uops:";
const char *perf_event_3_pretty_name = "DSB uops:";
const char *perf_event_4_pretty_name = "MS uops:";

/*
 * Cache event codes for faster performance.
 */
int perf_event_1_code = -1;
int perf_event_2_code = -1;
int perf_event_3_code = -1;
int perf_event_4_code = -1;

/*
 * Some PAPI functions don't seem to be thread safe...
 */
pthread_mutex_t papi_mutex;

/*
 * Utility function for reading MSRs.
 */
static int open_msr(int core) {
	char msr_filename[1024] = { '\0' };
	int fd = -1;
	
	snprintf(msr_filename, sizeof(msr_filename), "/dev/cpu/%d/msr", core);
	
	fd = open(msr_filename, O_RDONLY);
	if (fd < 0) {
		perror("open");
		fprintf(stderr, "open_msr failed while trying to open %s!\n", msr_filename);
		return fd;
	}
	
	return fd;
}

/*
 * Utility function for reading MSRs.
 */
static int read_msr(int fd, unsigned msr_offset, uint64_t *msr_out) {
	if (pread(fd, msr_out, sizeof(*msr_out), msr_offset) != sizeof(*msr_out)) {
		perror("pread");
		fprintf(stderr, "read_msr failed while trying to read offset 0x%04x!\n", msr_offset);
		return 0;
	}
	
	/* Success */
	return 1;
}

/*
 * Utility function for reading core temperatures.
 */
static short read_temp(int fd, unsigned msr_offset) {
	uint64_t msr_therm_status = 0;
	
	if (read_msr(fd, msr_offset, &msr_therm_status)) {
		return tjmax - ((msr_therm_status >> 16) & 0x7f);
	} else {
		fprintf(stderr, "Failed to read MSR offset 0x%04x\n", msr_offset);
		fprintf(stderr, "read_temp failed!\n");
		return -1;
	}
}

/*
 * Utility function for reading core voltages.
 */
static double read_voltage(int fd) {
	const double voltage_units = 0.0001220703125; // From Intel's manual: 1.0 / (2^13)
	uint64_t msr_perf_status = 0;
	
	if (read_msr(fd, MSR_PERF_STATUS, &msr_perf_status)) {
		unsigned voltage_raw = (msr_perf_status >> 32) & 0xFFFF;
		return voltage_raw * voltage_units;
	} else {
		fprintf(stderr, "Failed to read MSR offset 0x%04x\n", MSR_PERF_STATUS);
		fprintf(stderr, "read_voltage failed!\n");
		return -1;
	}
}

/*
 * Initialize the measurement framework. This needs to be executed before any threads are spawned.
 */
int measure_init_papi(int flags) {
	/* Ignore flags */
	(void)flags;
	int code = 0;
	
	/* NOTE: PAPI_library_init gets stuck if called by multiple threads! */
	if (PAPI_library_init(PAPI_VER_CURRENT) != PAPI_VER_CURRENT) {
		fprintf(stderr, "Error: PAPI library initialisation failed.\n");
		return 0;
	}
	
	/* Initialize the PAPI thread support */
	if (PAPI_thread_init(pthread_self) != PAPI_OK) {
		fprintf(stderr, "Error: PAPI_thread_init failed.\n");
		return 0;
	}
	
	/* Cache event codes for faster performance. */
	char *name = strdup(perf_event_1_name);
	if (PAPI_event_name_to_code(name, &code) == PAPI_OK) {
		perf_event_1_code = code;
	} else {
		fprintf(stderr, "Warning: No such event found \"%s\"!\n", name);
	}
	free(name);
	name = strdup(perf_event_2_name);
	if (PAPI_event_name_to_code(name, &code) == PAPI_OK) {
		perf_event_2_code = code;
	} else {
		fprintf(stderr, "Warning: No such event found \"%s\"!\n", name);
	}
	free(name);
	name = strdup(perf_event_3_name);
	if (PAPI_event_name_to_code(name, &code) == PAPI_OK) {
		perf_event_3_code = code;
	} else {
		fprintf(stderr, "Warning: No such event found \"%s\"!\n", name);
	}
	free(name);
	name = strdup(perf_event_4_name);
	if (PAPI_event_name_to_code(name, &code) == PAPI_OK) {
		perf_event_4_code = code;
	} else {
		fprintf(stderr, "Warning: No such event found \"%s\"!\n", name);
	}
	free(name);
	
	/* Initialize the mutex used to protect some calls to PAPI functions */
	pthread_mutex_init(&papi_mutex, NULL);
	
	/* Check whether we are running as root */
	if (geteuid() == 0) {
		running_as_root = 1;
	} else {
		running_as_root = 0;
		if (!(flags & MEASURE_FLAG_NO_PRINT)) {
			fprintf(stderr, "Warning: Not running as root, some functionality will be disabled.\n");
		}
	}
	
	if (running_as_root) {
		core0_fd = open_msr(0);
		core1_fd = open_msr(1);
		core2_fd = open_msr(2);
		core3_fd = open_msr(3);
		
		if (core0_fd >= 0) {
			uint64_t msr_temp_target = 0;
			if (read_msr(core0_fd, MSR_IA32_TEMPERATURE_TARGET, &msr_temp_target)) {
				unsigned tjmax_new = (msr_temp_target >> 16) & 0xff;
				/* printf("TjMax is %u degrees C\n", tjmax_new); */
				tjmax = tjmax_new;
			} else {
				fprintf(stderr, "Failed to read MSR_IA32_TEMPERATURE_TARGET!\n");
				fprintf(stderr, "Using the default value of %d for tjmax.", (int)tjmax);
			}
		}
	}
	
	/* Update the number of CPUs available */
	cpus_available = sysconf(_SC_NPROCESSORS_ONLN);
	
	/* Success */
	return 1;
}

/*
 * Simple warmup routine for making sure the dynamic linker resolves the needed symbols etc.
 */
static void measure_warmup(measure_state_t *state) {
	measure_start(state, 0);
	measure_stop(state, 0);
}

/*
 * Initialize performance measurements in worker threads.
 */
int measure_init_thread(measure_state_t *state, int flags) {
	int num_energy_events = 0;
	int num_perf_events = 0;
	char have_rapl = 1;
	int rval = 0;
	
	/* Initialize the state structure */
	memset(state, 0, sizeof(*state));
	state->pkg_power_before = 0.0;
	state->pp0_power_before = 0.0;
	state->pp1_power_before = 0.0;
	state->dram_power_before = 0.0;
	state->time_elapsed_before = 0.0;
	state->event_1_before = 0.0;
	state->event_2_before = 0.0;
	state->event_3_before = 0.0;
	state->event_4_before = 0.0;
	state->idx_pkg_energy = -1;
	state->idx_pp0_energy = -1;
	state->idx_pp1_energy = -1;
	state->idx_dram_energy = -1;
	state->idx_cycles = -1;
	state->idx_ref_cycles = -1;
	state->idx_instructions = -1;
	state->idx_event_1 = -1;
	state->idx_event_2 = -1;
	state->idx_event_3 = -1;
	state->idx_event_4 = -1;
	
	PAPI_register_thread();
	
	/* Disable energy measurements if requested */
	if (flags & MEASURE_FLAG_NO_ENERGY) {
		have_rapl = 0;
	}
	
	/* Disable energy measurements if not running as root */
	if (!running_as_root) {
		have_rapl = 0;
	}
	
	/* Find the RAPL component of PAPI. */
	int num_components = PAPI_num_components();
	int component_id = 0;
	const PAPI_component_info_t *component_info = NULL;
	if (have_rapl) {
		for (component_id = 0; component_id < num_components; ++component_id) {
			component_info = PAPI_get_component_info(component_id);
			if (component_info && strstr(component_info->name, "rapl")) {
				break;
			}
		}
		if (component_id == num_components) {
			fprintf(stderr, "Warning: No RAPL component found in PAPI library.\n");
			have_rapl = 0;
		} else if (component_info->disabled) {
			fprintf(stderr, "Warning: RAPL component of PAPI disabled: %s.\n", component_info->disabled_reason);
			have_rapl = 0;
		}
	}
	
	/* Create an event set. */
	state->papi_energy_events = PAPI_NULL;
	if ((rval = PAPI_create_eventset(&state->papi_energy_events)) != PAPI_OK) {
		fprintf(stderr, "Error: PAPI_create_eventset failed (rval = %d)!\n", rval);
		return 0;
	}
	
	state->papi_perf_events = PAPI_NULL;
	if ((rval = PAPI_create_eventset(&state->papi_perf_events)) != PAPI_OK) {
		fprintf(stderr, "Error: PAPI_create_eventset failed (rval = %d)!\n", rval);
		return 0;
	}
	
	int code = PAPI_NATIVE_MASK;
	if (have_rapl) {
		int retval = 0;
		for (retval = PAPI_enum_cmp_event(&code, PAPI_ENUM_FIRST, component_id); retval == PAPI_OK; retval = PAPI_enum_cmp_event(&code, PAPI_ENUM_EVENTS, component_id)) {
			char event_name[PAPI_MAX_STR_LEN];
			if (PAPI_event_code_to_name(code, event_name) != PAPI_OK) {
				fprintf(stderr, "Warning: Could not get PAPI event name.\n");
				continue;
			}
			
			PAPI_event_info_t event_info;
			if (PAPI_get_event_info(code, &event_info) != PAPI_OK) {
				fprintf(stderr, "Warning: Could not get PAPI event info.\n");
				continue;
			}
			if (event_info.data_type != PAPI_DATATYPE_UINT64) {
				continue;
			}
			
			if (strstr(event_name, "PACKAGE_ENERGY:")) {
				state->idx_pkg_energy = num_energy_events;
			} else if (strstr(event_name, "PP0_ENERGY:")) {
				state->idx_pp0_energy = num_energy_events;
			} else if (strstr(event_name, "PP1_ENERGY:")) {
				state->idx_pp1_energy = num_energy_events;
			} else if (strstr(event_name, "DRAM_ENERGY:")) {
				state->idx_dram_energy = num_energy_events;
			} else {
				continue; /* Skip other counters */
			}
			
#ifdef DEBUG
			printf("Adding %s to event set.\n", event_name);
#endif
			if (PAPI_add_event(state->papi_energy_events, code) != PAPI_OK) {
				break;
			}
			++num_energy_events;
		}
		if (num_energy_events == 0) {
			fprintf(stderr, "Warning: Could not find any RAPL events.\n");
		}
	}
	
	/* Fixed function performance counters */
	if ((rval = PAPI_add_event(state->papi_perf_events, PAPI_TOT_CYC)) == PAPI_OK) {
		state->idx_cycles = num_perf_events;
		++num_perf_events;
	} else {
		fprintf(stderr, "Warning: PAPI_add_event failed for PAPI_TOT_CYC (code = %d, rval = %d)!\n", PAPI_TOT_CYC, rval);
	}
#if 0
	/* PAPI_REF_CYC seems to use one of the programmable counters */
	if ((rval = PAPI_add_event(state->papi_perf_events, PAPI_REF_CYC)) == PAPI_OK) {
		state->idx_ref_cycles = num_perf_events;
		++num_perf_events;
	} else {
		fprintf(stderr, "Warning: PAPI_add_event failed for PAPI_REF_CYC (code = %d, rval = %d)!\n", PAPI_REF_CYC, rval);
	}
#endif
	if ((rval = PAPI_add_event(state->papi_perf_events, PAPI_TOT_INS)) == PAPI_OK) {
		state->idx_instructions = num_perf_events;
		++num_perf_events;
	} else {
		fprintf(stderr, "Warning: PAPI_add_event failed for PAPI_TOT_INS (code = %d, rval = %d)!\n", PAPI_TOT_INS, rval);
	}
	
	/*
	 * PAPI_add_event() seems to have issues with multiple threads...
	 */
	pthread_mutex_lock(&papi_mutex);
	
	/* Programmable counters */
	if ((rval = PAPI_add_event(state->papi_perf_events, perf_event_1_code)) == PAPI_OK) {
		state->idx_event_1 = num_perf_events;
		++num_perf_events;
	} else {
		fprintf(stderr, "PAPI_add_event failed for %s (rval = %d)!\n", perf_event_1_name, rval);
	}
	if ((rval = PAPI_add_event(state->papi_perf_events, perf_event_2_code)) == PAPI_OK) {
		state->idx_event_2 = num_perf_events;
		++num_perf_events;
	} else {
		fprintf(stderr, "PAPI_add_event failed for %s (rval = %d)!\n", perf_event_2_name, rval);
	}
	if ((rval = PAPI_add_event(state->papi_perf_events, perf_event_3_code)) == PAPI_OK) {
		state->idx_event_3 = num_perf_events;
		++num_perf_events;
	} else {
		fprintf(stderr, "PAPI_add_event failed for %s (rval = %d)!\n", perf_event_3_name, rval);
	}
	if ((rval = PAPI_add_event(state->papi_perf_events, perf_event_4_code)) == PAPI_OK) {
		state->idx_event_4 = num_perf_events;
		++num_perf_events;
	} else {
		fprintf(stderr, "PAPI_add_event failed for %s (rval = %d)!\n", perf_event_4_name, rval);
	}
	
	/*
	 * End of critical section...
	 */
	pthread_mutex_unlock(&papi_mutex);
	
	/* Store the numbers of events */
	state->num_energy_events = num_energy_events;
	state->num_perf_events = num_perf_events;
	state->have_rapl = have_rapl;
	
	/* Allocate buffers for reading the event sets */
	state->papi_energy_values = measure_alloc(num_energy_events * sizeof(*state->papi_energy_values));
	state->papi_perf_values = measure_alloc(num_perf_events * sizeof(*state->papi_perf_values));
	
	/* Run the warmup */
	measure_warmup(state);
	
	/* Success */
	return 1;
}

/*
 * Start measurements.
 */
int measure_start(measure_state_t *state, int flags) {
	/* Ignore flags */
	(void)flags;
	
	if (clock_gettime(CLOCK_REALTIME, &state->begin_time) < 0) {
		perror("clock_gettime");
	}
	
	/* Read the Time Stamp Counter value */
	{
		uint64_t tsc = 0;
		RDTSC(tsc);
		state->begin_tsc = tsc;
	}
	
	/* Read temperatures and voltages */
	if (core0_fd >= 0) {
		state->begin_temp_pkg = read_temp(core0_fd, MSR_IA32_PACKAGE_THERM_STATUS);
		state->begin_temp0 = read_temp(core0_fd, MSR_IA32_THERM_STATUS);
		state->begin_voltage0 = read_voltage(core0_fd);
	}
	if (core1_fd >= 0) {
		state->begin_temp1 = read_temp(core1_fd, MSR_IA32_THERM_STATUS);
		state->begin_voltage1 = read_voltage(core1_fd);
	}
	if (core2_fd >= 0) {
		state->begin_temp2 = read_temp(core2_fd, MSR_IA32_THERM_STATUS);
		state->begin_voltage2 = read_voltage(core2_fd);
	}
	if (core3_fd >= 0) {
		state->begin_temp3 = read_temp(core3_fd, MSR_IA32_THERM_STATUS);
		state->begin_voltage3 = read_voltage(core3_fd);
	}
	
	if (state->have_rapl) {
		if (PAPI_start(state->papi_energy_events) == PAPI_OK) {
			state->energy_started = 1;
		} else {
			fprintf(stderr, "Warning: PAPI_start failed for the energy events!\n");
		}
	}
	
	if (PAPI_start(state->papi_perf_events) == PAPI_OK) {
		state->perf_started = 1;
	} else {
		fprintf(stderr, "Warning: PAPI_start failed for the performance events!\n");
	}
	
	/* Success */
	return 1;
}

/*
 * Stop the measurements and do nothing else.
 */
int measure_stop(measure_state_t *state, int flags) {
	/* Flags are ignored */
	flags = flags;
	
	if (clock_gettime(CLOCK_REALTIME, &state->end_time) < 0) {
		perror("clock_gettime");
	}
	
	/* Read the Time Stamp Counter value */
	{
		uint64_t tsc = 0;
		RDTSC(tsc);
		state->end_tsc = tsc;
	}
	
	/* Read temperatures and voltages */
	if (core0_fd >= 0) {
		state->end_temp_pkg = read_temp(core0_fd, MSR_IA32_PACKAGE_THERM_STATUS);
		state->end_temp0 = read_temp(core0_fd, MSR_IA32_THERM_STATUS);
		state->end_voltage0 = read_voltage(core0_fd);
	}
	if (core1_fd >= 0) {
		state->end_temp1 = read_temp(core1_fd, MSR_IA32_THERM_STATUS);
		state->end_voltage1 = read_voltage(core1_fd);
	}
	if (core2_fd >= 0) {
		state->end_temp2 = read_temp(core2_fd, MSR_IA32_THERM_STATUS);
		state->end_voltage2 = read_voltage(core2_fd);
	}
	if (core3_fd >= 0) {
		state->end_temp3 = read_temp(core3_fd, MSR_IA32_THERM_STATUS);
		state->end_voltage3 = read_voltage(core3_fd);
	}
	
	long long *papi_energy_values = state->papi_energy_values;
	if (state->have_rapl) {
		if (PAPI_stop(state->papi_energy_events, papi_energy_values) == PAPI_OK) {
			state->energy_started = 0;
		} else {
			fprintf(stderr, "Warning: PAPI_stop failed for the energy events!\n");
		}
	}
	
	long long *papi_perf_values = state->papi_perf_values;
	if (PAPI_stop(state->papi_perf_events, papi_perf_values) == PAPI_OK) {
		state->perf_started = 0;
	} else {
		fprintf(stderr, "Warning: PAPI_stop failed for the performance events!\n");
	}
	
	/* Success */
	return 1;
}

/*
 * Function for combining result sets from different threads.
 */
int measure_combine_perf_results(measure_state_t *this, measure_state_t *other) {
	int i = 0;
	int num_perf_events = this->num_perf_events;
	
	/* Sanity check */
	if (num_perf_events != other->num_perf_events) {
		fprintf(stderr, "Error: %s: Event sets don't contain the same number of events!\n", __func__);
		return 0;
	}
	
	for (i = 0; i < num_perf_events; i++) {
		this->papi_perf_values[i] += other->papi_perf_values[i];
	}
	
	/* Success */
	return 1;
}

/*
 * Print the results after the measurement has been stopped.
 */
int measure_print(measure_state_t *state, int flags) {
	double pkg_power = 0, pp0_power = 0, pp1_power = 0, dram_power = 0;
	double million_cycles_per_second = 0, million_ref_cycles_per_second = 0, million_instructions_per_second = 0;
	double million_uops_per_second = 0, million_idq_mite_uops_per_second = 0, million_idq_dsb_uops_per_second = 0, million_idq_ms_uops_per_second = 0;
	long long *papi_energy_values = state->papi_energy_values;
	long long *papi_perf_values = state->papi_perf_values;
	char print_results = !(flags & MEASURE_FLAG_NO_PRINT);
	
	double time_elapsed = (state->end_time.tv_sec - state->begin_time.tv_sec) + (state->end_time.tv_nsec - state->begin_time.tv_nsec) * 1e-9;
	state->time_elapsed_before = time_elapsed;
	
	if (print_results) printf("Time elapsed: %12.6f seconds\n", time_elapsed);
	/* Print the TSC value */
	{
		unsigned long long tsc_elapsed = state->end_tsc - state->begin_tsc;
		double tsc_freq = tsc_elapsed / time_elapsed * 1e-9;
		if (print_results) printf("TSC elapsed:  %12llu\t(%12.3f GHz)\n", tsc_elapsed, tsc_freq);
	}
	if (state->have_rapl) {
		if (print_results) printf("\n");
		if (state->idx_pkg_energy != -1) {
			double pkg_energy = papi_energy_values[state->idx_pkg_energy] * ENERGY_SCALE_FACTOR;
			pkg_power = pkg_energy / time_elapsed;
			if (state->pkg_power_before != 0.0) {
				double power_delta = pkg_power - state->pkg_power_before;
				if (print_results) printf("PKG energy consumed:  %12.6f joules\t(%12.3f watts)\t[delta %+12.3f watts]\n", pkg_energy, pkg_power, power_delta);
			} else {
				if (print_results) printf("PKG energy consumed:  %12.6f joules\t(%12.3f watts)\n", pkg_energy, pkg_power);
			}
			state->pkg_power_before = pkg_power;
		}
		if (state->idx_pp0_energy != -1) {
			double pp0_energy = papi_energy_values[state->idx_pp0_energy] * ENERGY_SCALE_FACTOR;
			pp0_power = pp0_energy / time_elapsed;
			if (state->pp0_power_before != 0.0) {
				double power_delta = pp0_power - state->pp0_power_before;
				if (print_results) printf("PP0 energy consumed:  %12.6f joules\t(%12.3f watts)\t[delta %+12.3f watts]\n", pp0_energy, pp0_power, power_delta);
			} else {
				if (print_results) printf("PP0 energy consumed:  %12.6f joules\t(%12.3f watts)\n", pp0_energy, pp0_power);
			}
			state->pp0_power_before = pp0_power;
		}
		if (state->idx_pp1_energy != -1) {
			double pp1_energy = papi_energy_values[state->idx_pp1_energy] * ENERGY_SCALE_FACTOR;
			pp1_power = pp1_energy / time_elapsed;
			if (state->pp1_power_before != 0.0) {
				double power_delta = pp1_power - state->pp1_power_before;
				if (print_results) printf("PP1 energy consumed:  %12.6f joules\t(%12.3f watts)\t[delta %+12.3f watts]\n", pp1_energy, pp1_power, power_delta);
			} else {
				if (print_results) printf("PP1 energy consumed:  %12.6f joules\t(%12.3f watts)\n", pp1_energy, pp1_power);
			}
			state->pp1_power_before = pp1_power;
		}
		if (state->idx_dram_energy != -1) {
			double dram_energy = papi_energy_values[state->idx_dram_energy] * ENERGY_SCALE_FACTOR;
			dram_power = dram_energy / time_elapsed;
			if (state->dram_power_before != 0.0) {
				double power_delta = dram_power - state->dram_power_before;
				if (print_results) printf("DRAM energy consumed: %12.6f joules\t(%12.3f watts)\t[delta %+12.3f watts]\n", dram_energy, dram_power, power_delta);
			} else {
				if (print_results) printf("DRAM energy consumed: %12.6f joules\t(%12.3f watts)\n", dram_energy, dram_power);
			}
			state->dram_power_before = dram_power;
		}
	}
	if (print_results) {
		if (state->begin_temp_pkg != 0) {
			printf("\n");
			printf("Temp PKG:   %.0f  -->  %.0f\n", state->begin_temp_pkg, state->end_temp_pkg);
		}
		if (state->begin_temp0 != 0) {
			printf("Temp CORE0: %.0f  -->  %.0f\n", state->begin_temp0, state->end_temp0);
		}
		if (state->begin_temp1 != 0) {
			printf("Temp CORE1: %.0f  -->  %.0f\n", state->begin_temp1, state->end_temp1);
		}
		if (state->begin_temp2 != 0) {
			printf("Temp CORE2: %.0f  -->  %.0f\n", state->begin_temp2, state->end_temp2);
		}
		if (state->begin_temp3 != 0) {
			printf("Temp CORE3: %.0f  -->  %.0f\n", state->begin_temp3, state->end_temp3);
		}
		if (state->begin_voltage0 != 0) {
			printf("\n");
			printf("Voltage CORE0: %.4f  -->  %.4f\n", state->begin_voltage0, state->end_voltage0);
		}
		if (state->begin_voltage1 != 0) {
			printf("Voltage CORE1: %.4f  -->  %.4f\n", state->begin_voltage1, state->end_voltage1);
		}
		if (state->begin_voltage2 != 0) {
			printf("Voltage CORE2: %.4f  -->  %.4f\n", state->begin_voltage2, state->end_voltage2);
		}
		if (state->begin_voltage3 != 0) {
			printf("Voltage CORE3: %.4f  -->  %.4f\n", state->begin_voltage3, state->end_voltage3);
		}
		printf("\n");
	}
	if (state->idx_cycles != -1) {
		long long cycles_elapsed = papi_perf_values[state->idx_cycles];
		million_cycles_per_second = cycles_elapsed / time_elapsed * 1e-6;
		if (print_results) printf("%-26s%12lld\t(%12.3f M/sec)\n", "Cycles elapsed:", cycles_elapsed, million_cycles_per_second);
	}
	if (state->idx_ref_cycles != -1) {
		long long ref_cycles_elapsed = papi_perf_values[state->idx_ref_cycles];
		million_ref_cycles_per_second = ref_cycles_elapsed / time_elapsed * 1e-6;
		if (print_results) printf("%-26s%12lld\t(%12.3f M/sec)\n", "Reference cycles elapsed:", ref_cycles_elapsed, million_ref_cycles_per_second);
	}
	if (state->idx_instructions != -1) {
		long long instructions_retired = papi_perf_values[state->idx_instructions];
		million_instructions_per_second = instructions_retired / time_elapsed * 1e-6;
		if (print_results) printf("%-26s%12lld\t(%12.3f M/sec)\n", "Instructions retired:", instructions_retired, million_instructions_per_second);
	}
	if (state->idx_event_1 != -1) {
		long long uops_issued = papi_perf_values[state->idx_event_1];
		double uops_per_second = uops_issued / time_elapsed;
		million_uops_per_second = uops_per_second * 1e-6;
		state->event_1_before = uops_per_second;
		if (print_results) printf("%-26s%12lld\t(%12.3f M/sec)\n", perf_event_1_pretty_name, uops_issued, million_uops_per_second);
	}
	if (state->idx_event_2 != -1) {
		long long idq_mite_uops = papi_perf_values[state->idx_event_2];
		double idq_mite_uops_per_second = idq_mite_uops / time_elapsed;
		million_idq_mite_uops_per_second = idq_mite_uops_per_second * 1e-6;
		state->event_2_before = idq_mite_uops_per_second;
		if (print_results) printf("%-26s%12lld\t(%12.3f M/sec)\n", perf_event_2_pretty_name, idq_mite_uops, million_idq_mite_uops_per_second);
	}
	if (state->idx_event_3 != -1) {
		long long idq_dsb_uops = papi_perf_values[state->idx_event_3];
		double idq_dsb_uops_per_second = idq_dsb_uops / time_elapsed;
		million_idq_dsb_uops_per_second = idq_dsb_uops_per_second * 1e-6;
		state->event_3_before = idq_dsb_uops_per_second;
		if (print_results) printf("%-26s%12lld\t(%12.3f M/sec)\n", perf_event_3_pretty_name, idq_dsb_uops, million_idq_dsb_uops_per_second);
	}
	if (state->idx_event_4 != -1) {
		long long idq_ms_uops = papi_perf_values[state->idx_event_4];
		double idq_ms_uops_per_second = idq_ms_uops / time_elapsed;
		million_idq_ms_uops_per_second = idq_ms_uops_per_second * 1e-6;
		state->event_4_before = idq_ms_uops_per_second;
		if (print_results) printf("%-26s%12lld\t(%12.3f M/sec)\n", perf_event_4_pretty_name, idq_ms_uops, million_idq_ms_uops_per_second);
	}
#if 0
	if (print_results) {
		printf("\n");
		/* Tabs between fields allow easy pasting to Libreoffice */
		printf("Spreadsheet dump: %.6f\t%.3fe6\t%.3fe6\t%.3fe6\t%.3fe6\t%.3fe6\t%.3f\t%.3f\n", time_elapsed, million_instructions_per_second, million_uops_per_second, million_idq_mite_uops_per_second, million_idq_dsb_uops_per_second, million_idq_ms_uops_per_second, pkg_power, pp0_power);
	}
#endif
	
	/* Flush the output (helps when outputting to a file) */
	if (print_results) {
		fflush(stdout);
	}
	
	/* Success */
	return 1;
}

/*
 * Clean up data structures.
 */
int measure_cleanup(measure_state_t *state) {
	free(state->papi_energy_values);
	free(state->papi_perf_values);
	if (PAPI_cleanup_eventset(state->papi_energy_events) != PAPI_OK) {
		fprintf(stderr, "Warning: PAPI_cleanup_eventset failed!\n");
	}
	if (PAPI_cleanup_eventset(state->papi_perf_events) != PAPI_OK) {
		fprintf(stderr, "Warning: PAPI_cleanup_eventset failed!\n");
	}
	
	/* Success */
	return 1;
}

/*
 * Utility function for converting the result from gettimeofday() to a double.
 */
static double gettimeofday_double() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec * 1e-6;
}

/*
 * Utility function for allocating memory that is always wiped. Program execution is terminated in case of failure.
 */
void *measure_alloc(size_t size) {
	void *ptr = malloc(size);
	if (!ptr) {
		fprintf(stderr, "Error: malloc failed!\n");
		exit(EXIT_FAILURE);
		return NULL;
	}
	memset(ptr, 0, size);
	return ptr;
}

/*
 * Utility function for allocating aligned memory that is always wiped. Program execution is terminated in case of failure.
 */
void *measure_aligned_alloc(size_t size, size_t alignment) {
	void *ptr = NULL;
	if (posix_memalign(&ptr, alignment, size) != 0) {
		fprintf(stderr, "Error: posix_memalign failed!\n");
		exit(EXIT_FAILURE);
		return NULL;
	}
	memset(ptr, 0, size);
	return ptr;
}

/*
 * New higher level interface
 */

/*
 * Structure passed to the threads started with pthread_create().
 */
typedef struct {
	pthread_t thread_id;
	int (*benchmark)(void *benchdata, long ntimes);
	void *benchdata;
	long ntimes;
	measure_state_t measure_state;
	char do_measure;
} thread_args_t;

/*
 * Worker thread function
 */
static void *measure_benchmark_thread(void *arg) {
	thread_args_t *args = (thread_args_t *) arg;
	if (args->do_measure) {
		measure_init_thread(&args->measure_state, MEASURE_FLAG_NO_ENERGY);
		measure_start(&args->measure_state, 0);
	}
	args->benchmark(args->benchdata, args->ntimes);
	if (args->do_measure) {
		measure_stop(&args->measure_state, 0);
	}
	return NULL;
}

/*
 * Helper function for forcing thread affinity.
 */
static void measure_set_thread_affinity(pthread_attr_t *attr, int thread_num) {
	if (arg_force_affinity) {
		cpu_set_t mask;
		int cpu_num = thread_num % cpus_available;
		CPU_ZERO(&mask);
		CPU_SET(cpu_num, &mask);
		pthread_attr_setaffinity_np(attr, sizeof(mask), &mask);
	}
}

static void phase_warmup(measure_benchmark_t *bench, char quiet_mode, int (*warmup_func)(void *, long), thread_args_t *targs, pthread_attr_t *attrp) {
	long i = 0;
	int rval = 0;
	void *thread_result = NULL;
	
	/* Warmup phase */
	if ((arg_benchmark_phase == -1 || arg_benchmark_phase == 0) && arg_warmup_time > 0) {
		if (!quiet_mode) {
			printf("Running warmup for estimated %d seconds.\n", arg_warmup_time);
			fflush(stdout);
		}
		double warmup_start = gettimeofday_double();
		/* Calibration with the default ntimes value */
		for (i = 0; i < arg_num_threads; i++) {
			targs[i].benchmark = warmup_func;
			targs[i].ntimes = bench->ntimes;
			measure_set_thread_affinity(attrp, i);
			rval = pthread_create(&targs[i].thread_id, attrp, measure_benchmark_thread, &targs[i]);
			if (rval != 0) {
				fprintf(stderr, "Error: pthread_create failed (rval = %d)!\n", rval);
				exit(EXIT_FAILURE);
			}
		}
		for (i = 0; i < arg_num_threads; i++) {
			rval = pthread_join(targs[i].thread_id, &thread_result);
			if (rval != 0) {
				fprintf(stderr, "Warning: pthread_join failed (rval = %d)!\n", rval);
			}
			if (arg_do_measure) measure_cleanup(&targs[i].measure_state);
		}
		double warmup_calibration_end = gettimeofday_double();
		double warmup_calibration_duration = warmup_calibration_end - warmup_start;
		if (!quiet_mode) {
			printf("Warmup calibration of %ld iterations completed in %f seconds.\n", bench->ntimes, warmup_calibration_duration);
			fflush(stdout);
		}
		/* Estimate for ntimes to reach the requested warmup time */
		double ntimes_scale_factor = (arg_warmup_time - warmup_calibration_duration) / warmup_calibration_duration;
		if (ntimes_scale_factor > 0) {
			/* Launch threads again */
			for (i = 0; i < arg_num_threads; i++) {
				targs[i].ntimes *= ntimes_scale_factor;
				measure_set_thread_affinity(attrp, i);
				rval = pthread_create(&targs[i].thread_id, attrp, measure_benchmark_thread, &targs[i]);
				if (rval != 0) {
					fprintf(stderr, "Error: pthread_create failed (rval = %d)!\n", rval);
					exit(EXIT_FAILURE);
				}
			}
			for (i = 0; i < arg_num_threads; i++) {
				rval = pthread_join(targs[i].thread_id, &thread_result);
				if (rval != 0) {
					fprintf(stderr, "Warning: pthread_join failed (rval = %d)!\n", rval);
				}
				if (arg_do_measure) measure_cleanup(&targs[i].measure_state);
			}
		}
		double warmup_end = gettimeofday_double();
		if (!quiet_mode) {
			printf("Warmup complete in %f seconds.\n", warmup_end - warmup_start);
			fflush(stdout);
		}
	}
}

/*
 * Parsed command line parameters
 */
char arg_do_measure        = 0;
char arg_use_64bit_numbers = 0;
int  arg_benchmark_phase   = -1;
int  arg_num_threads       = 1;
int  arg_num_repeat        = 1;
int  arg_multiplier        = 1;
int  arg_warmup_time       = 120; /* 2 minutes */
char arg_force_affinity    = 0;

int measure_main(int argc, char **argv, measure_benchmark_t *bench) {
	long i = 0, j = 0;
	thread_args_t *targs = NULL;
	void *thread_result = NULL;
	int measure_flags = 0;
	int rval = 0;
	measure_state_t measure_state;
	char quiet_mode = 0;
	memset(&measure_state, 0, sizeof(measure_state));
	pthread_attr_t attr, *attrp = NULL;
	pthread_attr_init(&attr);
	
	/* Process command line arguments */
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-a") == 0) {
			/* Force the CPU affinity of benchmark threads */
			arg_force_affinity = 1;
		}
		else if (strcmp(argv[i], "-b") == 0) {
			/* Use either 64-bit integers or double-precision floating point */
			arg_use_64bit_numbers = 1;
		}
		else if (strcmp(argv[i], "-m") == 0) {
			/* Measure timing, performance and power consumption */
			arg_do_measure = 1;
		}
		else if (strcmp(argv[i], "-n") == 0) {
			/* Multiply the running time of micro benchmarks by a given factor */
			if (i + 1 < argc) {
				i++;
				arg_multiplier = atoi(argv[i]);
				bench->ntimes *= arg_multiplier;
			}
		}
		else if (strcmp(argv[i], "-p") == 0) {
			/* Only execute a specific benchmark phase (warmup = 0, normal = 1, or extreme = 2) */
			if (i + 1 < argc) {
				i++;
				arg_benchmark_phase = atoi(argv[i]);
			}
		}
		else if (strcmp(argv[i], "-r") == 0) {
			/* Number of times to repeat */
			if (i + 1 < argc) {
				i++;
				arg_num_repeat = atoi(argv[i]);
			}
		}
		else if (strcmp(argv[i], "-t") == 0) {
			/* Number of threads */
			if (i + 1 < argc) {
				i++;
				arg_num_threads = atoi(argv[i]);
			}
		}
		else if (strcmp(argv[i], "-w") == 0) {
			/* Warmup time in seconds */
			if (i + 1 < argc) {
				i++;
				arg_warmup_time = atoi(argv[i]);
			}
		}
		else {
			fprintf(stderr, "Error: Unrecognized option \"%s\".\n", argv[i]);
			exit(EXIT_FAILURE);
		}
	}
	
	if (arg_force_affinity) {
		attrp = &attr;
	}
	
	/* Seed random number generator with a constant seed to make the result reproducible */
	srand(0xdeadbeef);
	
	/* Less output when repeating */
	if (arg_num_repeat > 1) {
		quiet_mode = 1;
	}
	if (quiet_mode) {
		measure_flags |= MEASURE_FLAG_NO_PRINT;
	}
	
	if (arg_do_measure) {
		if (!measure_init_papi(measure_flags)) {
			fprintf(stderr, "Warning: measure_init_papi failed, disabling measurements.\n");
			arg_do_measure = 0;
		}
		if (!measure_init_thread(&measure_state, measure_flags)) {
			fprintf(stderr, "Warning: measure_init_thread failed, disabling measurements.\n");
			arg_do_measure = 0;
		}
	}
	
	/* Allocate data structures for threads */
	targs = measure_alloc(arg_num_threads * sizeof(*targs));
	if (targs == NULL) {
		fprintf(stderr, "Error: measure_alloc failed!\n");
		exit(EXIT_FAILURE);
	}
	
	/* Pre-warmup of all the benchmark hook functions */
	void *pre_warmup_benchdata = NULL;
	bench->init(&pre_warmup_benchdata);
	bench->normal(pre_warmup_benchdata, 0);
	bench->extreme(pre_warmup_benchdata, 0);
	bench->cleanup(pre_warmup_benchdata);
	
	/* Call initialization hook for every thread structure */
	for (i = 0; i < arg_num_threads; i++) {
		if (!bench->init(&targs[i].benchdata)) {
			fprintf(stderr, "Error: Benchmark initialization hook function failed!\n");
			exit(EXIT_FAILURE);
		}
		/* Copy arguments */
		targs[i].do_measure = arg_do_measure;
	}
	
	// Print CSV-output column names
	if (arg_num_repeat > 1) {
		printf("num_threads"
		       ",time_elapsed_normal,uops_issued_normal,idq_mite_normal,pkg_power_normal,pp0_power_normal,pkg_temp_normal"
		       ",time_elapsed_extreme,uops_issued_extreme,idq_mite_extreme,pkg_power_extreme,pp0_power_extreme,pkg_temp_extreme"
		       "\n");
		fflush(stdout);
	}
	
	/* Buffers for storing repeated measurements */
	const long buffer_size = arg_num_repeat * sizeof(double);
	double *pkg_power_normal = measure_alloc(buffer_size), *pp0_power_normal = measure_alloc(buffer_size);
	double *pkg_power_extreme = measure_alloc(buffer_size), *pp0_power_extreme = measure_alloc(buffer_size);
	double *time_elapsed_normal = measure_alloc(buffer_size), *time_elapsed_extreme = measure_alloc(buffer_size);
	double *uops_issued_normal = measure_alloc(buffer_size), *uops_issued_extreme = measure_alloc(buffer_size);
	double *idq_mite_uops_normal = measure_alloc(buffer_size), *idq_mite_uops_extreme = measure_alloc(buffer_size);
	double *pkg_temp_normal = measure_alloc(buffer_size), *pkg_temp_extreme = measure_alloc(buffer_size);
	
	/* Warmup for normal version */
	if (arg_benchmark_phase == -1 || arg_benchmark_phase == 1) {
		phase_warmup(bench, quiet_mode, bench->normal, targs, attrp);
	}
	
	/* Normal version */
	if (arg_benchmark_phase == -1 || arg_benchmark_phase == 2) {
		/* Repeat requested number of times */
		for (j = 0; j < arg_num_repeat; j++) {
			if (!quiet_mode) {
				printf("\n");
				printf("========================================================================\n");
				printf("\n");
				printf("Running %ld iterations of normal version\n", bench->ntimes);
				fflush(stdout);
			}
			if (arg_do_measure) measure_start(&measure_state, measure_flags);
			for (i = 0; i < arg_num_threads; i++) {
				targs[i].benchmark = bench->normal;
				targs[i].ntimes = bench->ntimes;
				measure_set_thread_affinity(attrp, i);
				rval = pthread_create(&targs[i].thread_id, attrp, measure_benchmark_thread, &targs[i]);
				if (rval != 0) {
					fprintf(stderr, "Error: pthread_create failed (rval = %d)!\n", rval);
					exit(EXIT_FAILURE);
				}
			}
			for (i = 0; i < arg_num_threads; i++) {
				rval = pthread_join(targs[i].thread_id, &thread_result);
				if (rval != 0) {
					fprintf(stderr, "Warning: pthread_join failed (rval = %d)!\n", rval);
				}
			}
			if (arg_do_measure) {
				measure_stop(&measure_state, measure_flags);
				for (i = 0; i < arg_num_threads; i++) {
					measure_combine_perf_results(&measure_state, &targs[i].measure_state);
					measure_cleanup(&targs[i].measure_state);
				}
				measure_print(&measure_state, measure_flags);
				pkg_power_normal[j] = measure_state.pkg_power_before;
				pp0_power_normal[j] = measure_state.pp0_power_before;
				time_elapsed_normal[j] = measure_state.time_elapsed_before;
				uops_issued_normal[j] = measure_state.event_1_before;
				idq_mite_uops_normal[j] = measure_state.event_2_before;
				pkg_temp_normal[j] = measure_state.end_temp_pkg; /* sample pkg temperature at the end */
			}
		}
	}
	
	/* Warmup for extreme version */
	if (arg_benchmark_phase == -1 || arg_benchmark_phase == 3) {
		if (!quiet_mode) {
			printf("\n");
			printf("========================================================================\n");
			printf("\n");
		}
		phase_warmup(bench, quiet_mode, bench->extreme, targs, attrp);
	}
	
	/* Extreme unrolled version */
	if (arg_benchmark_phase == -1 || arg_benchmark_phase == 4) {
		/* Repeat requested number of times */
		for (j = 0; j < arg_num_repeat; j++) {
			if (!quiet_mode) {
				printf("\n");
				printf("========================================================================\n");
				printf("\n");
				printf("Running %ld iterations of extreme unrolled version\n", bench->ntimes);
				fflush(stdout);
			}
			if (arg_do_measure) measure_start(&measure_state, measure_flags);
			for (i = 0; i < arg_num_threads; i++) {
				targs[i].benchmark = bench->extreme;
				targs[i].ntimes = bench->ntimes;
				measure_set_thread_affinity(attrp, i);
				rval = pthread_create(&targs[i].thread_id, attrp, measure_benchmark_thread, &targs[i]);
				if (rval != 0) {
					fprintf(stderr, "Error: pthread_create failed (rval = %d)!\n", rval);
					exit(EXIT_FAILURE);
				}
			}
			for (i = 0; i < arg_num_threads; i++) {
				rval = pthread_join(targs[i].thread_id, &thread_result);
				if (rval != 0) {
					fprintf(stderr, "Warning: pthread_join failed (rval = %d)!\n", rval);
				}
			}
			if (arg_do_measure) {
				measure_stop(&measure_state, measure_flags);
				for (i = 0; i < arg_num_threads; i++) {
					measure_combine_perf_results(&measure_state, &targs[i].measure_state);
					measure_cleanup(&targs[i].measure_state);
				}
				measure_print(&measure_state, measure_flags);
				pkg_power_extreme[j] = measure_state.pkg_power_before;
				pp0_power_extreme[j] = measure_state.pp0_power_before;
				time_elapsed_extreme[j] = measure_state.time_elapsed_before;
				uops_issued_extreme[j] = measure_state.event_1_before;
				idq_mite_uops_extreme[j] = measure_state.event_2_before;
				pkg_temp_extreme[j] = measure_state.end_temp_pkg; /* sample pkg temperature at the end */
			}
		}
	}
	
	/* Print compact power consumption numbers when repeating multiple times */
	if (arg_num_repeat > 1) {
		for (j = 0; j < arg_num_repeat; j++) {
			printf("%d,%f,%.0f,%.0f,%f,%f,%.0f,%f,%.0f,%.0f,%f,%f,%.0f\n", arg_num_threads,
				time_elapsed_normal[j], uops_issued_normal[j], idq_mite_uops_normal[j],
				pkg_power_normal[j], pp0_power_normal[j], pkg_temp_normal[j],
				time_elapsed_extreme[j], uops_issued_extreme[j], idq_mite_uops_extreme[j],
				pkg_power_extreme[j], pp0_power_extreme[j], pkg_temp_extreme[j]);
		}
		fflush(stdout);
	}
	
	/* Call cleanup hook for every thread structure */
	for (i = 0; i < arg_num_threads; i++) {
		bench->cleanup(targs[i].benchdata);
	}
	
	/* Clean up */
	free(pkg_power_normal);
	free(pp0_power_normal);
	free(pkg_power_extreme);
	free(pp0_power_extreme);
	free(time_elapsed_normal);
	free(time_elapsed_extreme);
	free(uops_issued_normal);
	free(uops_issued_extreme);
	free(idq_mite_uops_normal);
	free(idq_mite_uops_extreme);
	free(pkg_temp_normal);
	free(pkg_temp_extreme);
	if (arg_do_measure) measure_cleanup(&measure_state);
	free(targs);
	pthread_attr_destroy(&attr);
	
	/* Success */
	return EXIT_SUCCESS;
}
