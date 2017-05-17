
SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,%.o,$(SRCS))
OPTIONS = -Wall

ifdef O
  OPTIONS += -O$(O) -fno-omit-frame-pointer
else
  OPTIONS += -O0
endif

ifeq ($(GDB), 1)
  OPTIONS += -g
endif

all: mm_dac mm_dac_serial mm_dac_cilkplus fib fib_serial fib_cilkplus

mm_dac: mm_dac.o ktiming.o ../runtime/libcilkr.a
	gcc $^ -lpthread -o $@

mm_dac_serial: mm_dac_serial.o ktiming.o
	gcc $^ -o $@

mm_dac_cilkplus: mm_dac_cilkplus.o ktiming.o
	gcc $^ -lcilkrts -o $@

mm_dac_cilkplus.o: mm_dac_cilkplus.c
	gcc -c $(OPTIONS) -fcilkplus $<

fib: fib.o ktiming.o ../runtime/libcilkr.a
	gcc $^ -lpthread -o $@

fib_serial: fib_serial.o ktiming.o
	gcc $^ -o $@

fib_cilkplus: fib_cilkplus.o ktiming.o
	gcc $^ -lcilkrts -o $@

fib_cilkplus.o: fib_cilkplus.c
	gcc -c $(OPTIONS) -fcilkplus $<

%.o: %.c
	gcc -c $(OPTIONS) $<

clean:
	rm -f *.o *~ mm_dac mm_dac_serial mm_dac_cilkplus fib fib_serial fib_cilkplus
