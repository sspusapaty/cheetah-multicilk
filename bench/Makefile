include ../config.mk

SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,%.o,$(SRCS))

TESTS          = cholesky fib fft mm_dac matmul_4_way_z cilksort heat lu nqueens strassen
OPTIONS = -g -Werror -Wall $(OPT) -fcilkplus -ftapir=cilkr -fno-exceptions
# dynamic linking
# RTS_DLIBS = ../runtime/libcheetah.so
# RTS_DLIBS = -L../runtime -Wl,-rpath -Wl,../runtime -lcheetah
RTS_SLIBS = -L../runtime ../runtime/libcheetah.a
TIMING_COUNT := 1

all: $(TESTS)

$(TESTS): %: %.o getoptions.o ktiming.o
	$(CC) $^ -o $@ $(RTS_SLIBS) -lrt -lpthread -lm

%.o: %.c
	$(CC) -c $(OPTIONS) -DTIMING_COUNT=$(TIMING_COUNT) -o $@ $<

clean:
	rm -f *.o *~ $(TESTS) core.*
