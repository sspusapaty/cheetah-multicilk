#include <stdlib.h>

#undef main

extern int cilk_main(int argc, char* argv[]);

int main(int argc, char* argv[]) {
    int ret;

    fprintf(stderr, "Running the real main()\n"); 

    ret = cilk_main(argc, argv);

    return ret;
}
