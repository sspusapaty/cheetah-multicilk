include ../config.mk

SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,%.o,$(SRCS))

TESTS   = cilksort mm_dac fib
OPTIONS = -g -Wall $(OPT) -fno-omit-frame-pointer
# dynamic linking
# RTS_DLIBS = -L../runtime -Wl,-rpath -Wl,../runtime -lcheetah
# RTS_LIBS = ../runtime/libcheetah.so
# static linking
RTS_LIBS = ../runtime/libcheetah.a
TIMING_COUNT := 1

.PHONY: all check clean

all: $(TESTS)

$(TESTS): %: %.o ktiming.o getoptions.o
	$(CC) $^ -o $@ $(RTS_LIBS) -lrt -lpthread

%.o: %.c
	$(CC) -c $(OPTIONS) -DTIMING_COUNT=$(TIMING_COUNT) -o $@ $<

check:
	./fib --nproc 16 40
	./mm_dac --nproc 16 -n 1024 -c
	./cilksort --nproc 16 -n 30000000 -c

clean:
	rm -f *.o *~ $(TESTS) core.*
