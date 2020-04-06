include ../config.mk

# With reducers
REDUCER_INC = -I../include
REDUCER_DEF = -DREDUCER_MODULE

#ALERT_DEF = -DALERT_LVL=0x000

ABI = -DOPENCILK_ABI

MAIN = libcheetah
SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)
OBJS = $(patsubst %.c,./build/%.o,$(SRCS))
ARCH=-mavx2
OPT = -O1 -g
INCLUDES = $(REDUCER_INC)
DEFINES = $(REDUCER_DEF) $(ABI) $(ALERT_DEF)
OPTIONS = $(OPT) $(ARCH) -Werror -Wall -fpic $(DEFINES) $(INCLUDES)

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
