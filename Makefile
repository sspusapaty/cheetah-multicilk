OPTIONS =

ifdef O
  OPTIONS += O=$(O)
endif

ifdef GDB
  OPTIONS += GDB=$(GDB)
endif

ifdef ALERT
  OPTIONS += ALERT=$(ALERT)
endif

all:
	$(MAKE) -C runtime $(OPTIONS)
	$(MAKE) -C handcomp_test $(OPTIONS)

rebuild:
	$(MAKE) -C runtime clean
	$(MAKE) -C runtime $(OPTIONS)
	$(MAKE) -C handcomp_test clean
	$(MAKE) -C handcomp_test $(OPTIONS)

clean:
	$(MAKE) -C handcomp_test clean
	$(MAKE) -C runtime clean
