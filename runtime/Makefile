MAIN = libcilkr
SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,%.o,$(SRCS))
OPTIONS = -g -std=gnu99 -Wall -fpic

ifdef O
  OPTIONS += -O$(O) -fno-omit-frame-pointer
else
  OPTIONS += -O0 -fno-omit-frame-pointer
endif

ifeq ($(GDB), 1)
  OPTIONS += -g
endif

ifdef ALERT
  OPTIONS += -D"ALERT_LVL=$(ALERT)"
endif

$(MAIN).a: $(OBJS)
	ar rcs $@ $^

%.o: %.c $(HDRS)
	gcc -c $(OPTIONS) $<

clean:
	rm -f *.o *.a *~
