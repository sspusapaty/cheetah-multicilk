
SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,%.o,$(SRCS))

TESTS          = mm_dac fib fiber_debug
TESTS_CILKR    = $(patsubst %,%_cilkr,$(TESTS))
TESTS_SERIAL   = $(patsubst %,%_serial,$(TESTS))
TESTS_CILKPLUS = $(patsubst %,%_cilkplus,$(TESTS))

OPTIONS = -Wall

ifdef O
  OPTIONS += -O$(O) -fno-omit-frame-pointer
else
  OPTIONS += -O0 -fno-omit-frame-pointer
endif

ifeq ($(GDB), 1)
  OPTIONS += -g
endif

all: $(TESTS_CILKR) $(TESTS_SERIAL) $(TESTS_CILKPLUS)

$(TESTS_CILKR): %: %.o ktiming.o ../runtime/libcilkr.a
	gcc $^ -lpthread -o $@

$(TESTS_SERIAL): %: %.o ktiming.o
	gcc $^ -o $@

$(TESTS_CILKPLUS): %: %.o ktiming.o
	gcc $^ -lcilkrts -o $@

%_cilkplus.o: %_cilkplus.c
	gcc -c $(OPTIONS) -fcilkplus $<

%.o: %.c
	gcc -c $(OPTIONS) $<

clean:
	rm -f *.o *~ $(TESTS_CILKR) $(TESTS_SERIAL) $(TESTS_CILKPLUS)
