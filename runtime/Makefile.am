MAIN = libcilkr_main

$(MAIN).a: $(MAIN).o
	ar rcs $@ $^

$(MAIN).o: $(MAIN).c
	gcc -c -Wall -fpic $(MAIN).c

