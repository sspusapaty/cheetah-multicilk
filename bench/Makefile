include ../config.mk

SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,%.o,$(SRCS))

TESTS          = mm_dac fib
OPTIONS = -g -Werror -Wall $(OPT) -ftapir=cilkr -fno-exceptions
# dynamic linking
# RTS_DLIBS = ../runtime/libcheetah.so
# RTS_DLIBS = -L../runtime -Wl,-rpath -Wl,../runtime -lcheetah
RTS_SLIBS = -L../runtime ../runtime/libcheetah.a

all: $(TESTS)

$(TESTS): %: %.o ktiming.o
	$(CC) $^ -o $@ $(RTS_SLIBS) -lrt -lpthread

%.o: %.c
	$(CC) -c $(OPTIONS) -o $@ $<

clean:
	rm -f *.o *~ $(TESTS) core.*
