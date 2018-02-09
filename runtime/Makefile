include ../config.mk

MAIN = libcheetah
SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,%.o,$(SRCS))
OPTIONS = -g -Werror -Wall -fpic -g # -std=gnu99 -fno-omit-frame-pointer

.PHONY: all clean
 
all: $(MAIN).a $(MAIN).so

$(MAIN).a: $(OBJS)
	ar rcs $@ $^

$(MAIN).so: $(OBJS)
	$(CC) -shared -o $@ $^

%.o: %.c $(HDRS)
	$(CC) -c $(OPTIONS) $<

clean:
	rm -f *.o *.a *~
