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

all: $(TESTS)

$(TESTS): %: %.o ktiming.o getoptions.o
	$(CC) $^ -o $@ $(RTS_LIBS) -lrt -lpthread

%.o: %.c
	$(CC) -c $(OPTIONS) -DTIMING_COUNT=$(TIMING_COUNT) -o $@ $<

clean:
	rm -f *.o *~ $(TESTS) core.*
