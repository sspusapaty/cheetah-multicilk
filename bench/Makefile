include ../config.mk
# include ../cilkplus-config.mk

TEST_WORKERS=

SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,%.o,$(SRCS))

TESTS   = cholesky fib fft mm_dac matmul_4_way_z cilksort heat lu nqueens strassen
OPTIONS = $(OPT) $(ARCH) $(SAN) $(DBG) $(RTS_OPT) -fno-exceptions -Werror -W
# dynamic linking
LD_FLAGS = $(RTS_OPT) $(SAN)
# static linking
# LD_FLAGS = $(RTS_OPTS) -static-libopencilk $(SAN)
TIMING_COUNT := 3

all: $(TESTS)

$(TESTS): %: %.o getoptions.o ktiming.o
	$(LINK_CC) $^ -o $@ $(LD_FLAGS) -lm

%.o: %.c
	$(CC) -c $(OPTIONS) -DTIMING_COUNT=$(TIMING_COUNT) -o $@ $<

# for generating easy-to-read LLVM IR code
%.ll: %.c
	$(CC) -c $(OPTIONS) -S -emit-llvm -ftapir=none -DTIMING_COUNT=$(TIMING_COUNT) -o $@ $<

clean:
	rm -f *.o *~ $(TESTS) core.* *.ll

.PHONY: test

test:: cholesky
	CILK_NWORKERS=$(TEST_WORKERS) ./cholesky -c

test:: fib
	CILK_NWORKERS=$(TEST_WORKERS) ./fib -c 41

test:: fft
	CILK_NWORKERS=$(TEST_WORKERS) ./fft -c

test:: mm_dac
	CILK_NWORKERS=$(TEST_WORKERS) ./mm_dac -c

# strassen -c is too slow so use -rc
test:: strassen
	CILK_NWORKERS=$(TEST_WORKERS) ./strassen -rc

test:: nqueens
	CILK_NWORKERS=$(TEST_WORKERS) ./nqueens -c

test:: lu
	CILK_NWORKERS=$(TEST_WORKERS) ./lu -c

test:: cilksort
	CILK_NWORKERS=$(TEST_WORKERS) ./cilksort -c

test:: matmul_4_way_z
	CILK_NWORKERS=$(TEST_WORKERS) ./matmul_4_way_z -c
