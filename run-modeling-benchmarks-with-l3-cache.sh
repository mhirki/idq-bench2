#!/bin/sh

# Benchmark cases suitable for modeling

function run_benchmarks {
	./idq-bench-float32-add "$@"
	./idq-bench-float32-addmul "$@"
	./idq-bench-float32-array-l1-add "$@"
	./idq-bench-float32-array-l1-addmul "$@"
	./idq-bench-float32-array-l1-scale "$@"
	./idq-bench-float32-array-l1-schoenauer "$@"
	./idq-bench-float32-array-l1-triad "$@"
	./idq-bench-float32-array-l2-add "$@"
	./idq-bench-float32-array-l2-addmul "$@"
	./idq-bench-float32-array-l2-scale "$@"
	./idq-bench-float32-array-l2-schoenauer "$@"
	./idq-bench-float32-array-l2-triad "$@"
	./idq-bench-float32-array-l3-add "$@"
	./idq-bench-float32-array-l3-addmul "$@"
	./idq-bench-float32-array-l3-scale "$@"
	./idq-bench-float32-array-l3-schoenauer "$@"
	./idq-bench-float32-array-l3-triad "$@"
	./idq-bench-float32-scale "$@"
	./idq-bench-float32-schoenauer "$@"
	./idq-bench-float-add "$@"
	./idq-bench-float-addmul "$@"
	./idq-bench-float-array-l1-add "$@"
	./idq-bench-float-array-l1-addmul "$@"
	./idq-bench-float-array-l1-scale "$@"
	./idq-bench-float-array-l1-schoenauer "$@"
	./idq-bench-float-array-l1-triad "$@"
	./idq-bench-float-array-l2-add "$@"
	./idq-bench-float-array-l2-addmul "$@"
	./idq-bench-float-array-l2-scale "$@"
	./idq-bench-float-array-l2-schoenauer "$@"
	./idq-bench-float-array-l2-triad "$@"
	./idq-bench-float-array-l3-add "$@"
	./idq-bench-float-array-l3-addmul "$@"
	./idq-bench-float-array-l3-scale "$@"
	./idq-bench-float-array-l3-schoenauer "$@"
	./idq-bench-float-array-l3-triad "$@"
	./idq-bench-float-scale "$@"
	./idq-bench-float-schoenauer "$@"
	./idq-bench-int32-array-l1-addmulshift "$@"
	./idq-bench-int32-array-l1-addmulshift2 "$@"
	./idq-bench-int32-array-l2-addmulshift "$@"
	./idq-bench-int32-array-l2-addmulshift2 "$@"
	./idq-bench-int32-array-l3-addmulshift "$@"
	./idq-bench-int32-array-l3-addmulshift2 "$@"
	./idq-bench-int-algo-prng "$@"
	./idq-bench-int-algo-prng-multi2 "$@"
	./idq-bench-int-algo-prng-multi3 "$@"
	./idq-bench-int-algo-prng-multi3b "$@"
	./idq-bench-int-algo-prng-multi3c "$@"
	./idq-bench-int-algo-prng-multi4 "$@"
	./idq-bench-int-algo-prng-multi4b "$@"
	./idq-bench-int-array-l1-addmul "$@"
	./idq-bench-int-array-l1-addmulshift "$@"
	./idq-bench-int-array-l1-addmulshift2 "$@"
	./idq-bench-int-array-l1-addmulshift3 "$@"
	./idq-bench-int-array-l1-addmulshift4 "$@"
	./idq-bench-int-array-l2-addmul "$@"
	./idq-bench-int-array-l2-addmulshift "$@"
	./idq-bench-int-array-l2-addmulshift2 "$@"
	./idq-bench-int-array-l2-addmulshift3 "$@"
	./idq-bench-int-array-l2-addmulshift4 "$@"
	./idq-bench-int-array-l3-addmul "$@"
	./idq-bench-int-array-l3-addmulshift "$@"
	./idq-bench-int-array-l3-addmulshift2 "$@"
	./idq-bench-int-array-l3-addmulshift3 "$@"
	./idq-bench-int-array-l3-addmulshift4 "$@"
}

# Run benchmarks with 1 thread for ~11 seconds
run_benchmarks -t 1 -w 0 -n 10

# Run benchmarks with 2 threads for ~11 seconds
run_benchmarks -t 2 -w 0 -n 10

# Run benchmarks with 3 threads for ~11 seconds
run_benchmarks -t 3 -w 0 -n 10

# Run benchmarks with 4 threads for ~11 seconds
run_benchmarks -t 4 -w 0 -n 10
