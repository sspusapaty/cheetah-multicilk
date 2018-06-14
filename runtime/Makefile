include ../config.mk

MAIN = libcheetah
SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,./build/%.o,$(SRCS))
OPTIONS = -O3 -g -Werror -Wall -fpic -g

.PHONY: all clean build
 
all: build $(MAIN).a $(MAIN).so

$(MAIN).a: $(OBJS)
	ar rcs $@ $^

$(MAIN).so: $(OBJS)
	$(CC) -shared -o $@ $^

build:
	mkdir -p $@

build/%.o: %.c $(HDRS)
	$(CC) -c $(OPTIONS) -o $@ $<

clean:
	rm -f $(OBJS) *.a *.so *~
