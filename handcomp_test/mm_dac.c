#include <stdio.h>
#include <stdlib.h>

#include "../runtime/cilk.h"
#include "ktiming.h"


#ifndef TIMES_TO_RUN
#define TIMES_TO_RUN 1 
#endif

#define CHECK_RESULT 1
#define THRESHOLD 16
#define TRUE 1
#define FALSE 0

unsigned int randomSeed = 1;

static void mm_dac_serial(int *C, const int *A, const int *B, int n, int length) {

    if(length < THRESHOLD) {
        // Use a loop for small matrices
        for (int i = 0; i < length; i++)
            for (int j = 0; j < length; j++)
                for (int k = 0; k < length; k++)
                    C[i*n+j] += A[i*n+k] * B[k*n+j];
        return;
    }

    // Partition the matrices
    int mid = length >> 1;

    int *C00 = C;
    int *C01 = C + mid;
    int *C10 = C + n*mid;
    int *C11 = C + n*mid + mid;

    int const *A00 = A;
    int const *A01 = A + mid;
    int const *A10 = A + n*mid;
    int const *A11 = A + n*mid + mid;

    int const *B00 = B;
    int const *B01 = B + mid;
    int const *B10 = B + n*mid;
    int const *B11 = B + n*mid + mid;

    mm_dac_serial(C00, A00, B00, n, mid);
    mm_dac_serial(C01, A00, B01, n, mid);
    mm_dac_serial(C10, A10, B00, n, mid);
    mm_dac_serial(C11, A10, B01, n, mid);

    mm_dac_serial(C00, A01, B10, n, mid);
    mm_dac_serial(C01, A01, B11, n, mid);
    mm_dac_serial(C10, A11, B10, n, mid);
    mm_dac_serial(C11, A11, B11, n, mid);
}

__attribute__((noinline)) static void 
mm_dac_spawn_helper(int *C, const int *A, const int *B, int n, int length);

/**
 * Recursive implementation of matrix multiply.
 * This code will not work on non-square matrices.
 * Effect: Compute C+=A*B,
 * where C, A, and B are the starting addresses of submatrices with dimension
 * length x length.  Argument n is the original input matrix length.
 **/
static void mm_dac(int *C, const int *A, const int *B, int n, int length) {

    if(length < THRESHOLD) {
        // Use a loop for small matrices
        for (int i = 0; i < length; i++)
            for (int j = 0; j < length; j++)
                for (int k = 0; k < length; k++)
                    C[i*n+j] += A[i*n+k] * B[k*n+j];
        return;
    }

    alloca(ZERO);
    __cilkrts_stack_frame sf;
    __cilkrts_enter_frame(&sf);

    // Partition the matrices
    int mid = length >> 1;

    int *C00 = C;
    int *C01 = C + mid;
    int *C10 = C + n*mid;
    int *C11 = C + n*mid + mid;

    int const *A00 = A;
    int const *A01 = A + mid;
    int const *A10 = A + n*mid;
    int const *A11 = A + n*mid + mid;

    int const *B00 = B;
    int const *B01 = B + mid;
    int const *B10 = B + n*mid;
    int const *B11 = B + n*mid + mid;

    __cilkrts_save_fp_ctrl_state(&sf);
    if(!__builtin_setjmp(sf.ctx)) {
        mm_dac_spawn_helper(C00, A00, B00, n, mid);
    }
    __cilkrts_save_fp_ctrl_state(&sf);
    if(!__builtin_setjmp(sf.ctx)) {
        mm_dac_spawn_helper(C01, A00, B01, n, mid);
    }
    __cilkrts_save_fp_ctrl_state(&sf);
    if(!__builtin_setjmp(sf.ctx)) {
        mm_dac_spawn_helper(C10, A10, B00, n, mid);
    }
    __cilkrts_save_fp_ctrl_state(&sf);
    if(!__builtin_setjmp(sf.ctx)) {
        mm_dac_spawn_helper(C11, A10, B01, n, mid);
    }

    if(sf.flags & CILK_FRAME_UNSYNCHED) {
        __cilkrts_save_fp_ctrl_state(&sf);
        if(!__builtin_setjmp(sf.ctx)) {
            __cilkrts_sync(&sf);
        }
    }

    __cilkrts_save_fp_ctrl_state(&sf);
    if(!__builtin_setjmp(sf.ctx)) {
        mm_dac_spawn_helper(C00, A01, B10, n, mid);
    }
    __cilkrts_save_fp_ctrl_state(&sf);
    if(!__builtin_setjmp(sf.ctx)) {
        mm_dac_spawn_helper(C01, A01, B11, n, mid);
    }
    __cilkrts_save_fp_ctrl_state(&sf);
    if(!__builtin_setjmp(sf.ctx)) {
        mm_dac_spawn_helper(C10, A11, B10, n, mid);
    }
    __cilkrts_save_fp_ctrl_state(&sf);
    if(!__builtin_setjmp(sf.ctx)) {
        mm_dac_spawn_helper(C11, A11, B11, n, mid);
    }

    if(sf.flags & CILK_FRAME_UNSYNCHED) {
        __cilkrts_save_fp_ctrl_state(&sf);
        if(!__builtin_setjmp(sf.ctx)) {
            __cilkrts_sync(&sf);
        }
    }

    __cilkrts_pop_frame(&sf);
    __cilkrts_leave_frame(&sf);
}

__attribute__((noinline))
static void mm_dac_spawn_helper(int *C, const int *A, const int *B, int n, int length) {

    __cilkrts_stack_frame sf;
    __cilkrts_enter_frame_fast(&sf);
    __cilkrts_detach(&sf);
    mm_dac(C, A, B, n, length);
    __cilkrts_pop_frame(&sf);
    __cilkrts_leave_frame(&sf); 
}

static void rand_matrix(int *dest, int n) {
    for(int i = 0; i < n*n; ++i)
        dest[i] = rand_r(&randomSeed) & 0xff;
}

static void zero_matrix(int *dest, int n) {
    for(int i = 0; i < n*n; ++i)
        dest[i] = 0;
}

#if CHECK_RESULT
static int are_equal_matrices(const int *a, const int *b, int n) {
    for(int i = 0; i < n*n; ++i)
        if(a[i] != b[i])
            return FALSE;
    return TRUE;
}
#endif

static void test_mm(int n) {
    clockmark_t begin, end;
    uint64_t running_time[TIMES_TO_RUN];

    int *A = (int *) malloc(sizeof(int)*(n*n));
    int *B = (int *) malloc(sizeof(int)*(n*n));
    int *C = (int *) malloc(sizeof(int)*(n*n));

    rand_matrix(A, n);
    rand_matrix(B, n);
    zero_matrix(C, n);

    for(int i = 0; i < TIMES_TO_RUN; i++) {
        begin = ktiming_getmark();
        mm_dac(C, A, B, n, n);
        end = ktiming_getmark();
        running_time[i] = ktiming_diff_usec(&begin, &end);
    }
    print_runtime(running_time, TIMES_TO_RUN);

#if CHECK_RESULT
    int * Cs = (int*) malloc(sizeof(int) * (n*n));
    zero_matrix(Cs, n);
    mm_dac_serial(Cs, A, B, n, n);
    if(!are_equal_matrices(C, Cs, n)) {
        fprintf(stderr, "MM_dac test FAILED.\n");
    } else {
        fprintf(stderr, "MM_dac test passed.\n");
    }
    free(Cs);
#endif

    free(C);
    free(B);
    free(A);
}

// return true iff n = 2^k (or 0).
static int is_power_of_2(int n) {
    return (n & (n-1)) == 0;
}

int cilk_main(int argc, char *argv[]) {
    int N = -1;

    if(argc != 2) {
        fprintf(stderr, "Usage: mm_dac <n>\n");
        exit(1);
    }

    N = atoi(argv[1]);
    if(!is_power_of_2(N)) {
        fprintf(stderr, "N must be a power of 2 \n");
        exit(1);
    }
    test_mm(N);

    return 0;
}
