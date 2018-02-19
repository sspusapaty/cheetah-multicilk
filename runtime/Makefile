include ../config.mk

MAIN = libcheetah
SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,build/%.o,$(SRCS))
OPTIONS = -g -Werror -Wall -fpic -g

.PHONY: all clean
 
all: $(MAIN).a $(MAIN).so

$(MAIN).a: $(OBJS)
	ar rcs $@ $^

$(MAIN).so: $(OBJS)
	$(CC) -shared -o $@ $^

build/%.o: %.c $(HDRS)
	$(CC) -c $(OPTIONS) -o $@ $<

clean:
	rm -f $(OBJS) *.a *.so *~
