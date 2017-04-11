
SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,%.o,$(SRCS))

all: fib

fib: fib.o
	gcc fib.o ../runtime/libcilkr.a -o fib

%.o: %.c
	gcc -c -Wall $<
