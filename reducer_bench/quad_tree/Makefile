include ../../config.mk

OPTIONS  = -g -Wall $(OPT) $(RTS_OPT)

# RTS_LIBS = ../../runtime/$(RTS_LIB).a $(patsubst %,../../runtime/%/module.a,$(MODS))
RTS_LIBS = $(RTS_OPT) $(patsubst %,../../runtime/%/module.a,$(MODS))

# The sources we're building
HDRS = $(wildcard *.h)
SRCS = $(filter-out graphic_stuff.c, $(wildcard *.c))

# What we're building
OBJS = $(SRCS:.c=.o)

# What we're building with
CXX = $(CC)
CXXFLAGS = -std=gnu99 -Wall
LDFLAGS = -lrt -lm $(RTS_LIBS) -lpthread


# Determine which profile--debug or release--we should build against, and set
# CFLAGS appropriately.

ifneq ($(DEBUG),1)
  # We want release mode.
  OPTIONS += -DNDEBUG
endif

ifdef MAX_SIZE
  OPTIONS += -D"MAX_SIZE=$(MAX_SIZE)"
endif

ifdef MAX_DEPTH
  OPTIONS += -D"MAX_DEPTH=$(MAX_DEPTH)"
endif

ifdef X_CELLS
  OPTIONS += -D"X_CELLS=$(X_CELLS)"
endif

ifdef Y_CELLS
  OPTIONS += -D"Y_CELLS=$(Y_CELLS)"
endif

ifdef CELLS
  OPTIONS += -D"X_CELLS=$(CELLS)" -D"Y_CELLS=$(CELLS)"
endif

ifdef LIVE
  OPTIONS += -DLIVE
endif

all: screensaver

#$(PRODUCT): LDFLAGS += -lX11
screensaver: $(OBJS) graphic_stuff.o
	$(CC) $^ -o $@ $(RTS_LIBS) -lrt -lm -lX11
#	$(CXX) $(LDFLAGS) $(EXTRA_LDFLAGS) -o $@ $(OBJS) graphic_stuff.o

%.o: %.c $(HDRS)
	$(CC) -c $(OPTIONS) -o $@ $<
#	$(CXX) $(CXXFLAGS) $(EXTRA_CXXFLAGS) -o $@ -c $<

check: screensaver
	CILK_NWORKERS=2 ./screensaver 1000

clean:
	rm -f screensaver *.o *.out *~ core.*
