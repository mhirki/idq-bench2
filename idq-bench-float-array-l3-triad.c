/*
 * Benchmark designed to stress the instruction decoders. Designed for Intel Haswell microarchitecture. Compiled with GCC 4.4.
 *
 * Usage: ./idq-bench-float-array-l3-triad [ -b ] [ -m ] [ -n <running time multiplier> ] [ -r <number of times to repeat> ]
 *
 * Author: Mikael Hirki <mikael.hirki@aalto.fi>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

#include "measure-util.h"

/*
 * The arrays should fit in the nearest L3 cache segment, which is 2 MB on Intel processors.
 * 2 arrays * 65536 elements/array * 8 bytes/element = 1 MB
 */
#define ARRAY_SIZE	65536

/*
 * Align arrays to a 2 MB boundary.
 */
#define ARRAY_ALIGNMENT	2097152

/*
 * Loop enough times to make the power consumption measurable.
 */
#define NTIMES		20000

/*
 * Data type used in the benchmark kernels.
 */
typedef double kernel_data_t;

/* Exponential macro expansion */
#define ADD_1 magic += a[j] + scalar * b[j]; j++;
#define ADD_2 ADD_1 ADD_1
#define ADD_4 ADD_2 ADD_2
#define ADD_8 ADD_4 ADD_4
#define ADD_16 ADD_8 ADD_8
#define ADD_32 ADD_16 ADD_16
#define ADD_64 ADD_32 ADD_32
#define ADD_128 ADD_64 ADD_64
#define ADD_256 ADD_128 ADD_128
#define ADD_512 ADD_256 ADD_256
#define ADD_1024 ADD_512 ADD_512
#define ADD_2048 ADD_1024 ADD_1024

/*
 * Benchmark kernels
 */
kernel_data_t kernel_warmup(long ntimes, kernel_data_t *a, kernel_data_t *b, kernel_data_t scalar) {
	long i = 0, j = 0;
	kernel_data_t magic = 0;
	for (i = 0; i < ntimes; i++) {
		for (j = 0; j < ARRAY_SIZE;) {
			ADD_128
		}
	}
	return magic;
}

kernel_data_t kernel_normal(long ntimes, kernel_data_t *a, kernel_data_t *b, kernel_data_t scalar) {
	long i = 0, j = 0;
	kernel_data_t magic = 0;
	for (i = 0; i < ntimes; i++) {
		for (j = 0; j < ARRAY_SIZE;) {
			ADD_128
		}
	}
	return magic;
}

kernel_data_t kernel_extreme(long ntimes, kernel_data_t *a, kernel_data_t *b, kernel_data_t scalar) {
	long i = 0, j = 0;
	kernel_data_t magic = 0;
	for (i = 0; i < ntimes; i++) {
		for (j = 0; j < ARRAY_SIZE;) {
			ADD_1024
		}
	}
	return magic;
}

typedef struct {
	kernel_data_t *a;
	kernel_data_t *b;
	kernel_data_t scalar;
} benchdata_t;

static int bench_init(void **benchdata) {
	benchdata_t *data = calloc(1, sizeof(benchdata_t));
	*benchdata = data;
	
	/* Allocate memory for the data arrays */
	data->a = measure_aligned_alloc(2 * ARRAY_SIZE * sizeof(kernel_data_t), ARRAY_ALIGNMENT);
	data->b = data->a + ARRAY_SIZE;
	data->scalar = 3;
	
	/* Success */
	return 1;
}

static int bench_warmup(void *benchdata, long ntimes) {
	benchdata_t *data = benchdata;
	return kernel_warmup(ntimes, data->a, data->b, data->scalar);
}

static int bench_normal(void *benchdata, long ntimes) {
	benchdata_t *data = benchdata;
	return kernel_normal(ntimes, data->a, data->b, data->scalar);
}

static int bench_extreme(void *benchdata, long ntimes) {
	benchdata_t *data = benchdata;
	return kernel_extreme(ntimes, data->a, data->b, data->scalar);
}

static int bench_cleanup(void *benchdata) {
	benchdata_t *data = benchdata;
	free(data->a);
	free(data);
	
	/* Success */
	return 1;
}

int main(int argc, char **argv) {
	measure_benchmark_t bench;
	memset(&bench, 0, sizeof(bench));
	
	/* Set up benchmark parameters */
	bench.ntimes = NTIMES;
	bench.init = bench_init;
	bench.warmup = bench_warmup;
	bench.normal = bench_normal;
	bench.extreme = bench_extreme;
	bench.cleanup = bench_cleanup;
	
	return measure_main(argc, argv, &bench);
}
