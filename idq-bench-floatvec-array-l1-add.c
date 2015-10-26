/*
 * Benchmark designed to stress the instruction decoders. Designed for Intel Haswell microarchitecture. Compiled with GCC 4.4.
 *
 * Usage: ./idq-bench-floatvec-array-l1-add [ -b ] [ -m ] [ -n <running time multiplier> ] [ -r <number of times to repeat> ]
 *
 * Author: Mikael Hirki <mikael.hirki@aalto.fi>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <immintrin.h>

#include "measure-util.h"

/*
 * The arrays should fit in L1 cache, which is 32 kB on Intel processors.
 * 1 array * 2048 elements/array * 8 bytes/element = 16 kB
 */
#define ARRAY_SIZE	2048
#define NUM_ARRAYS	1

/*
 * Align arrays to a 2 MB boundary.
 */
#define ARRAY_ALIGNMENT	2097152

/*
 * Loop enough times to make the power consumption measurable.
 */
#define NTIMES		606000

/*
 * Data type used in the benchmark kernels.
 */
typedef double kernel_data_t;

/* Exponential macro expansion */
#define ADD_1 sum_128 += a_128[j]; j++;
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
kernel_data_t kernel_normal(long ntimes, kernel_data_t *a) {
	long i = 0, j = 0;
	__m128d sum_128 = (__m128d){ 0.0, 0.0 };
	__m128d *a_128 = (__m128d *)a;
	for (i = 0; i < ntimes; i++) {
		for (j = 0; j < ARRAY_SIZE / 2;) {
			ADD_256
		}
	}
	double sum_as_double[2];
	_mm_store_pd((double *)&sum_as_double, sum_128);
	double sum = sum_as_double[0] + sum_as_double[1];
	return sum;
}

kernel_data_t kernel_extreme(long ntimes, kernel_data_t *a) {
	long i = 0, j = 0;
	__m128d sum_128 = (__m128d){ 0.0, 0.0 };
	__m128d *a_128 = (__m128d *)a;
	for (i = 0; i < ntimes; i++) {
		for (j = 0; j < ARRAY_SIZE / 2;) {
			ADD_512
		}
	}
	double sum_as_double[2];
	_mm_store_pd((double *)&sum_as_double, sum_128);
	double sum = sum_as_double[0] + sum_as_double[1];
	return sum;
}

typedef struct {
	kernel_data_t *a;
} benchdata_t;

static int bench_init(void **benchdata) {
	benchdata_t *data = calloc(1, sizeof(benchdata_t));
	*benchdata = data;
	kernel_data_t *a = NULL;
	long i = 0;
	
	/* Allocate memory for the data arrays */
	data->a = a = measure_aligned_alloc(NUM_ARRAYS * ARRAY_SIZE * sizeof(kernel_data_t), ARRAY_ALIGNMENT);
	
	/* Fill with random numbers */
	if (arg_use_64bit_numbers) {
		for (i = 0; i < NUM_ARRAYS * ARRAY_SIZE; i++) {
			a[i] = rand64();
		}
	} else {
		for (i = 0; i < NUM_ARRAYS * ARRAY_SIZE; i++) {
			a[i] = (float)rand();
		}
	}
	
	/* Success */
	return 1;
}

static int bench_normal(void *benchdata, long ntimes) {
	benchdata_t *data = benchdata;
	return kernel_normal(ntimes, data->a);
}

static int bench_extreme(void *benchdata, long ntimes) {
	benchdata_t *data = benchdata;
	return kernel_extreme(ntimes, data->a);
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
	bench.normal = bench_normal;
	bench.extreme = bench_extreme;
	bench.cleanup = bench_cleanup;
	
	return measure_main(argc, argv, &bench);
}