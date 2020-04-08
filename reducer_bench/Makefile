include ../config.mk

#	$(CC) -O0 -g -ftapir=cilkr main.c -c -o main.o
#	$(CC) main.o ../runtime/libcheetah.a ../runtime/reducer/module.a -lpthread -o main

SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,%.o,$(SRCS))

MANY = 8 # how many cores is a lot?
ENABLE_X11 = false

CTESTS   = intlist serialsum intsum multispawnsum repeatedintsum
CXXTESTS = cppsum
DIRTESTS = nqueens # quad_tree
TESTS    = $(CTESTS) $(CXXTESTS) $(DIRTESTS)
ABI      = -fopencilk # or -fcilk
WARN     = -W -Wno-mismatched-tags -Wno-unused-parameter -Werror
OPTIONS  = -g3 -Wall $(OPT) -fno-omit-frame-pointer $(ABI) $(WARN)

RTS_LIBS = ../runtime/libcheetah.a $(patsubst %,../runtime/%/module.a,$(MODS))

TIMING_COUNT := 1

.PHONY: all check clean $(DIRTESTS)

all: $(TESTS)

$(CTESTS): %: %.o ktiming.o getoptions.o
	$(CC) $^ -o $@ $(RTS_LIBS) -lrt -lpthread -lm

$(CXXTESTS): %: %.o ktiming.o getoptions.o
	$(CXX) $^ -o $@ $(RTS_LIBS) -lrt -lpthread -lm

$(DIRTESTS):
	$(MAKE) -C $@ $(TOPASS)

%.o: %.c
	$(CC) -c $(OPTIONS) -DTIMING_COUNT=$(TIMING_COUNT) -o $@ $<

%.o: %.cpp
	$(CXX) -c $(OPTIONS) -DTIMING_COUNT=$(TIMING_COUNT) -o $@ $<

check:
	$(MAKE) clean; $(MAKE) TIMING_COUNT=5 $(TOPASS) > /dev/null 2>&1
	./intlist --nproc 2 40000000
	./serialsum --nproc 1 200000000
	./intsum --nproc 1 200000000
	./intsum --nproc 2 200000000
	./multispawnsum --nproc 2 100000000
	./cppsum --nproc 2 200000000
	$(MAKE) -C nqueens check $(TOPASS)
#	$(MAKE) -C quad_tree check $(TOPASS)

stress:
	$(MAKE) clean; $(MAKE) TIMING_COUNT=5 $(TOPASS) > /dev/null 2>&1
	./intlist --nproc $(MANY) 40000000
	./intsum --nproc $(MANY) 200000000
	# Assertion failure: SPA resize not supported yet!
	#./repeatedintsum --nproc $(MANY) 10000000
	if $(ENABLE_X11); then $(MAKE) -C quad_tree MANY="$(MANY)" check $(TOPASS) ; else : ; fi

#redcheck:
#	$(MAKE) clean; $(MAKE) TIMING_COUNT=1 $(TOPASS) > /dev/null 2>&1
#	./intlist --nproc 2 2048
#redgdb:
#	$(MAKE) clean; $(MAKE) TIMING_COUNT=1 $(TOPASS) > /dev/null 2>&1
#	gdb --args ./intlist --nproc 2 2048

clean:
	rm -f *.o *~ $(CTESTS) $(CXXTESTS) core.*
	$(foreach test,$(DIRTESTS),$(MAKE) -C $(test) clean;)

