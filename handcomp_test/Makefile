include ../config.mk

SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,%.o,$(SRCS))

TESTS   = cilksort fib mm_dac nqueens
OPTIONS = -g -Wall $(OPT) -fno-omit-frame-pointer
# dynamic linking
# RTS_DLIBS = -L../runtime -Wl,-rpath -Wl,../runtime -lcheetah
# RTS_LIBS = ../runtime/libcheetah.so
# static linking
RTS_LIBS = ../runtime/libcheetah.a
TIMING_COUNT := 1

.PHONY: all check memcheck clean

all: $(TESTS)

$(TESTS): %: %.o ktiming.o getoptions.o
	$(CC) $^ -o $@ $(RTS_LIBS) -lrt -lpthread -lm

%.o: %.c
	$(CC) -c $(OPTIONS) -DTIMING_COUNT=$(TIMING_COUNT) -o $@ $<

memcheck: 
	make clean; make >& /dev/null
	date
	valgrind ./fib --nproc 8 26
	valgrind ./mm_dac --nproc 8 -n 512 
	valgrind ./cilksort --nproc 8 -n 3000000
	valgrind ./nqueens --nproc 8 10
	date

check:
	make clean; make TIMING_COUNT=5 >& /dev/null
	./fib --nproc 16 40
	./mm_dac --nproc 16 -n 1024 -c
	./cilksort --nproc 16 -n 30000000 -c
	./nqueens --nproc 16 14

clean:
	rm -f *.o *~ $(TESTS) core.*
