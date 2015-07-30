CFLAGS = -pthread -Wall -Wextra -O2 -g
CC = gcc
LIBS_PAPI = -lpapi
LIBS = -lrt $(LIBS_PAPI)

BINARY_TARGETS = idq-bench-float-addmul idq-bench-float-array-l1-addmul idq-bench-float-array-l2-addmul idq-bench-float-array-l3-addmul \
                 idq-bench-float-add idq-bench-float-array-l1-add idq-bench-float-array-l2-add idq-bench-float-array-l3-add \
                 idq-bench-float-schoenauer idq-bench-float-array-l1-schoenauer idq-bench-float-array-l2-schoenauer idq-bench-float-array-l3-schoenauer \
                 idq-bench-float-array-l1-triad idq-bench-float-array-l2-triad idq-bench-float-array-l3-triad \
                 idq-bench-float-scale idq-bench-float-array-l1-scale idq-bench-float-array-l2-scale idq-bench-float-array-l3-scale \
                 idq-bench-int-array-l1-addmul idq-bench-int-array-l2-addmul idq-bench-int-array-l3-addmul \
                 idq-bench-int-algo-prng idq-bench-int-algo-prng-multi2 idq-bench-int-algo-prng-multi3 idq-bench-int-algo-prng-multi3b idq-bench-int-algo-prng-multi3c idq-bench-int-algo-prng-multi4

all: $(BINARY_TARGETS)

.PHONY: clean

clean:
	rm -f $(BINARY_TARGETS) measure-util.o

measure-util.o: measure-util.c
	$(CC) -c $(CFLAGS) -o $@ $<

# Implicit rule for making executable binaries
%: %.c measure-util.o
	$(CC) $(CFLAGS) -o $@ $< measure-util.o $(LIBS)
