
SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,%.o,$(SRCS))

all: fib

fib: fib.o ktiming.o ../runtime/libcilkr.a
	gcc fib.o ktiming.o ../runtime/libcilkr.a -lpthread -o fib

%.o: %.c
	gcc -c -Wall $<

clean:
	rm -f *.o *~ fib
