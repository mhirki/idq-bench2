/*
 * Benchmark designed to stress the instruction decoders. Designed for Intel Haswell microarchitecture. Compiled with GCC 4.4.
 *
 * Usage: ./idq-bench-int-algo-prng-multi2 [ -b ] [ -m ] [ -n <running time multiplier> ] [ -r <number of times to repeat> ]
 *
 * Author: Mikael Hirki <mikael.hirki@gmail.com>
 *
 * Copyright (c) 2015 Helsinki Institute of Physics
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

#include "measure-util.h"

/*
 * Number of inner loop iterations.
 */
#define ARRAY_SIZE	2048

/*
 * Loop enough times to make the power consumption measurable.
 */
#define NTIMES		454000

/*
 * Data type used in the benchmark kernels.
 */
typedef unsigned long long kernel_data_t;

/* Exponential macro expansion */
#define ADD_1 magic *= 1103515245; magic += 12345; magic2 *= 1664525; magic2 += 1013904223; j++;
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
kernel_data_t kernel_normal(long ntimes) {
	long i = 0, j = 0;
	kernel_data_t magic = 0, magic2 = 0;
	for (i = 0; i < ntimes; i++) {
		for (j = 0; j < ARRAY_SIZE;) {
			ADD_128
		}
	}
	return magic + magic2;
}

kernel_data_t kernel_extreme(long ntimes) {
	long i = 0, j = 0;
	kernel_data_t magic = 0, magic2 = 0;
	for (i = 0; i < ntimes; i++) {
		for (j = 0; j < ARRAY_SIZE;) {
			ADD_1024
		}
	}
	return magic + magic2;
}

typedef struct {
	kernel_data_t dummy;
} benchdata_t;

static int bench_init(void **benchdata) {
	benchdata_t *data = calloc(1, sizeof(benchdata_t));
	*benchdata = data;

	/* Success */
	return 1;
}

static int bench_normal(void *benchdata, long ntimes) {
	(void)benchdata;
	return kernel_normal(ntimes);
}

static int bench_extreme(void *benchdata, long ntimes) {
	(void)benchdata;
	return kernel_extreme(ntimes);
}

static int bench_cleanup(void *benchdata) {
	benchdata_t *data = benchdata;
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
