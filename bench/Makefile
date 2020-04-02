include ../config.mk
# include ../cilkplus-config.mk

SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,%.o,$(SRCS))

TESTS          = cholesky fib fft mm_dac matmul_4_way_z cilksort heat lu nqueens strassen
OPT=-O3
OPTIONS = $(OPT) -g -fcilkplus $(RTS_OPT) -fno-exceptions # -Werror -Wall 
# dynamic linking
RTS_DLIBS += -L$(RTS_DIR) -Wl,-rpath -Wl,$(RTS_DIR) $(RTS_LIB_FLAG)  
LD_FLAGS = $(RTS_DLIBS)
# static linking
# RTS_SLIBS = $(RTS_DIR)/$(RTS_LIB).a
# LD_FLAGS = $(RTS_SLIBS)
TIMING_COUNT := 3 

all: $(TESTS)

$(TESTS): %: %.o getoptions.o ktiming.o
	$(LINK_CC) $^ -o $@ $(LD_FLAGS) -lrt -lpthread -lm -ldl

%.o: %.c
	$(CC) -c $(OPTIONS) -DTIMING_COUNT=$(TIMING_COUNT) -o $@ $<

clean:
	rm -f *.o *~ $(TESTS) core.*
