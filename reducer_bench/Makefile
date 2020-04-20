include ../config.mk

SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,%.o,$(SRCS))

MANY = 8 # how many cores is a lot?
ENABLE_X11 = false

CTESTS   = intlist serialsum intsum multispawnsum repeatedintsum
CXXTESTS = cppsum
DIRTESTS = nqueens # quad_tree
TESTS    = $(CTESTS) $(CXXTESTS) $(DIRTESTS)
WARN     = -W -Wno-mismatched-tags -Wno-unused-parameter -Werror
OPTIONS  = $(OPT) $(ARCH) $(DBG) -fno-omit-frame-pointer $(RTS_OPT) $(WARN)

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
	$(MAKE) TIMING_COUNT=5 $(TOPASS)
	./intlist --nproc 2 40000000
	./serialsum --nproc 1 200000000
	./intsum --nproc 1 200000000
	./intsum --nproc $(MANY) 200000000
	./multispawnsum --nproc 2 100000000
	./cppsum --nproc 2 200000000
	$(MAKE) -C nqueens check $(TOPASS)
	if $(ENABLE_X11); then $(MAKE) -C quad_tree check $(TOPASS) ; else : ; fi

stress:
	$(MAKE) TIMING_COUNT=5 $(TOPASS)
	./intlist --nproc $(MANY) 40000000
	./intsum --nproc $(MANY) 200000000
	if $(ENABLE_X11); then $(MAKE) -C quad_tree check $(TOPASS) ; else : ; fi
#	 Assertion failure: SPA resize not supported yet!
#	./repeatedintsum --nproc $(MANY) 10000000

#redcheck:
#	$(MAKE) clean; $(MAKE) TIMING_COUNT=1 $(TOPASS) > /dev/null 2>&1
#	./intlist --nproc 2 2048
#redgdb:
#	$(MAKE) clean; $(MAKE) TIMING_COUNT=1 $(TOPASS) > /dev/null 2>&1
#	gdb --args ./intlist --nproc 2 2048

clean:
	rm -f *.o *~ $(CTESTS) $(CXXTESTS) core.*
	$(foreach test,$(DIRTESTS),$(MAKE) -C $(test) clean;)

cppsum.o: ktiming.h
intlist.o: ktiming.h
intsum.o: ktiming.h
ktiming.o: ktiming.h
multispawnsum.o: ktiming.h
repeatedintsum.o: ktiming.h
serialsum.o: ktiming.h
