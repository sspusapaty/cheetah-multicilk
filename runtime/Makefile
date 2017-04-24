MAIN = libcilkr
SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,%.o,$(SRCS))

$(MAIN).a: $(OBJS)
	ar rcs $@ $^

%.o: %.c $(HDRS)
	gcc -O0 -g -c -Wall -fpic $<

clean:
	rm -f *.o *.a
