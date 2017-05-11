MAIN = libcilkr
SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,%.o,$(SRCS))
OPTIONS = -Wall -fpic

ifdef BUG
  OPTIONS += -D"DEBUG_LVL=$(DEBUG)"
endif

$(MAIN).a: $(OBJS)
	ar rcs $@ $^

%.o: %.c $(HDRS)
	gcc -O0 -g -c $(OPTIONS) $<

clean:
	rm -f *.o *.a *~
