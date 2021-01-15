#include <cilk/cilk.h>
#include <cilk/cilk_api.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "getoptions.h"
#include "ktiming.h"

#ifndef RAND_MAX
#define RAND_MAX 32767
#endif

#ifndef TIMING_COUNT
#define TIMING_COUNT 1
#endif

#define DATA_TYPE int
static int base_size;  // the base case of the computation (2*base_power)
static int base_power; // the power of two the base case is based on
#define timing

static const unsigned int Q[] =
    {0x55555555, 0x33333333, 0x0F0F0F0F, 0x00FF00FF};
static const unsigned int S[] = {1, 2, 4, 8};

unsigned long rand_nxt = 0;

static int cilk_rand(void) {
    int result;
    rand_nxt = rand_nxt * 1103515245 + 12345;
    result = (rand_nxt >> 16) % ((unsigned int)RAND_MAX + 1);
    return result;
}

#if 0
static void mm_dac_serial(DATA_TYPE *C, const DATA_TYPE *A,
                          const DATA_TYPE *B, int n, int length) {

#define THRESH 16

    if (length < THRESH) {
        // Use a loop for small matrices
        for (int i = 0; i < length; i++)
            for (int j = 0; j < length; j++)
                for (int k = 0; k < length; k++)
                    C[i * n + j] += A[i * n + k] * B[k * n + j];
        return;
    }

    // Partition the matrices
    int mid = length >> 1;

    DATA_TYPE *C00 = C;
    DATA_TYPE *C01 = C + mid;
    DATA_TYPE *C10 = C + n * mid;
    DATA_TYPE *C11 = C + n * mid + mid;

    DATA_TYPE const *A00 = A;
    DATA_TYPE const *A01 = A + mid;
    DATA_TYPE const *A10 = A + n * mid;
    DATA_TYPE const *A11 = A + n * mid + mid;

    DATA_TYPE const *B00 = B;
    DATA_TYPE const *B01 = B + mid;
    DATA_TYPE const *B10 = B + n * mid;
    DATA_TYPE const *B11 = B + n * mid + mid;

    mm_dac_serial(C00, A00, B00, n, mid);
    mm_dac_serial(C01, A00, B01, n, mid);
    mm_dac_serial(C10, A10, B00, n, mid);
    mm_dac_serial(C11, A10, B01, n, mid);

    mm_dac_serial(C00, A01, B10, n, mid);
    mm_dac_serial(C01, A01, B11, n, mid);
    mm_dac_serial(C10, A11, B10, n, mid);
    mm_dac_serial(C11, A11, B11, n, mid);
}
#endif

static int are_equal_matrices(const DATA_TYPE *a, const DATA_TYPE *b, int n) {
    for (int i = 0; i < n * n; ++i)
        if (a[i] != b[i])
            return 0;
    return 1;
}

// provides a look up for the Morton Number of the z-order curve given the x and
// y coordinate every instance of an (x,y) lookup must use this function
unsigned int z_convert(int row, int col) {

    unsigned int z; // z gets the resulting 32-bit Morton Number.
    // x and y must initially be less than 65536.
    // The top and the left boundary

    col = (col | (col << S[3])) & Q[3];
    col = (col | (col << S[2])) & Q[2];
    col = (col | (col << S[1])) & Q[1];
    col = (col | (col << S[0])) & Q[0];

    row = (row | (row << S[3])) & Q[3];
    row = (row | (row << S[2])) & Q[2];
    row = (row | (row << S[1])) & Q[1];
    row = (row | (row << S[0])) & Q[0];

    z = col | (row << 1);

    return z;
}

// converts (x,y) position in the array to the mixed z-order row major layout
int block_convert(int row, int col) {
    int block_index = z_convert(row >> base_power, col >> base_power);
    return (block_index * base_size << base_power) +
           ((row - ((row >> base_power) << base_power)) << base_power) +
           (col - ((col >> base_power) << base_power));
}

// init the matric in order
void order(DATA_TYPE *M, int n) {
    int i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            M[block_convert(i, j)] = i * n + j;
        }
    }
}

// init the matric in order
void order_rm(DATA_TYPE *M, int n) {
    int i;
    for (i = 0; i < n * n; i++) {
        M[i] = i;
    }
}

// init the matrix to all ones
void one(DATA_TYPE *M, int n) {
    int i;
    for (i = 0; i < n * n; i++) {
        M[i] = 1.0;
    }
}

// init the matrix to all zeros
void zero(DATA_TYPE *M, int n) {
    int i;
    for (i = 0; i < n * n; i++) {
        M[i] = 0.0;
    }
}

// init the matrix to random numbers
void init(DATA_TYPE *M, int n) {
    int i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            M[block_convert(i, j)] = (DATA_TYPE)cilk_rand();
        }
    }
}

// init the matrix to random numbers
void init_rm(DATA_TYPE *M, int n) {
    int i;
    for (i = 0; i < n * n; i++) {
        M[i] = (DATA_TYPE)cilk_rand();
    }
}

// prints the matrix
void print_matrix(DATA_TYPE *M, int n) {
    int i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            printf("%6d ", M[block_convert(i, j)]);
        }
        printf("\n");
    }
}

// prints the matrix
void print_matrix_rm(DATA_TYPE *M, int n, int orig_n) {
    int i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            printf("%6d ", M[i * orig_n + j]);
        }
        printf("\n");
    }
}

#if 0
// itterative solution for matrix multiplication
void iter_matmul(DATA_TYPE *A, DATA_TYPE *B, DATA_TYPE *C, int n) {
    int i, j, k;

    for (i = 0; i < n; i++) {
        for (k = 0; k < n; k++) {
            DATA_TYPE c = 0.0;
            for (j = 0; j < n; j++) {
                c += A[block_convert(i, j)] * B[block_convert(j, k)];
            }
            C[block_convert(i, k)] = c;
        }
    }
}

// itterative solution for matrix multiplication
void iter_matmul_rm(DATA_TYPE *A, DATA_TYPE *B, DATA_TYPE *C, int n) {
    int i, j, k;

    for (i = 0; i < n; i++) {
        for (k = 0; k < n; k++) {
            DATA_TYPE c = 0.0;
            for (j = 0; j < n; j++) {
                c += A[i * n + j] * B[j * n + k];
            }
            C[i * n + k] = c;
        }
    }
}

// calculates the max error between the itterative solution and other solution
double maxerror(DATA_TYPE *M1, DATA_TYPE *M2, int n) {
    int i, j;
    double err = 0.0;

    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            double diff = (M1[block_convert(i, j)] - M2[block_convert(i, j)]) /
                          M1[block_convert(i, j)];
            if (diff < 0) {
                diff = -diff;
            }
            if (diff > err) {
                err = diff;
            }
        }
    }

    return err;
}

// calculates the max error between the itterative solution and other solution
double maxerror_rm(DATA_TYPE *M1, DATA_TYPE *M2, int n) {
    int i, j;
    double err = 0.0;

    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            double diff = (M1[i * n + j] - M2[i * n + j]) / M1[i * n + j];
            if (diff < 0) {
                diff = -diff;
            }
            if (diff > err) {
                err = diff;
            }
        }
    }

    return err;
}
#endif

static void mat_mul_seq(DATA_TYPE *A, DATA_TYPE *B, DATA_TYPE *C, int n) {

    if (n == base_size) {
        int i, j, k;
        for (i = 0; i < n; i++) {
            for (k = 0; k < n; k++) {
                DATA_TYPE c = 0.0;
                for (j = 0; j < n; j++) {
                    c += A[i * n + j] * B[j * n + k];
                }
                C[i * n + k] += c;
            }
        }
        return;
    }

    // partition each matrix into 4 sub matrices
    // each sub-matrix points to the start of the z pattern
    DATA_TYPE *A1 = &A[block_convert(0, 0)];
    DATA_TYPE *A2 = &A[block_convert(0, n >> 1)]; // bit shift to divide by 2
    DATA_TYPE *A3 = &A[block_convert(n >> 1, 0)];
    DATA_TYPE *A4 = &A[block_convert(n >> 1, n >> 1)];

    DATA_TYPE *B1 = &B[block_convert(0, 0)];
    DATA_TYPE *B2 = &B[block_convert(0, n >> 1)];
    DATA_TYPE *B3 = &B[block_convert(n >> 1, 0)];
    DATA_TYPE *B4 = &B[block_convert(n >> 1, n >> 1)];

    DATA_TYPE *C1 = &C[block_convert(0, 0)];
    DATA_TYPE *C2 = &C[block_convert(0, n >> 1)];
    DATA_TYPE *C3 = &C[block_convert(n >> 1, 0)];
    DATA_TYPE *C4 = &C[block_convert(n >> 1, n >> 1)];

    // recrusively call the sub-matrices for evaluation in parallel
    mat_mul_seq(A1, B1, C1, n >> 1);
    mat_mul_seq(A1, B2, C2, n >> 1);
    mat_mul_seq(A3, B1, C3, n >> 1);
    mat_mul_seq(A3, B2, C4, n >> 1);

    mat_mul_seq(A2, B3, C1, n >> 1);
    mat_mul_seq(A2, B4, C2, n >> 1);
    mat_mul_seq(A4, B3, C3, n >> 1);
    mat_mul_seq(A4, B4, C4, n >> 1);
}

// recursive parallel solution to matrix multiplication
void mat_mul_par(DATA_TYPE *A, DATA_TYPE *B, DATA_TYPE *C, int n) {
    // BASE CASE: here computation is switched to itterative matrix
    // multiplication At the base case A, B, and C point to row order matrices
    // of n x n
    if (n == base_size) {
        int i, j, k;
        for (i = 0; i < n; i++) {
            for (k = 0; k < n; k++) {
                DATA_TYPE c = 0.0;
                for (j = 0; j < n; j++) {
                    c += A[i * n + j] * B[j * n + k];
                }
                C[i * n + k] += c;
            }
        }
        return;
    }

    // partition each matrix into 4 sub matrices
    // each sub-matrix points to the start of the z pattern
    DATA_TYPE *A1 = &A[block_convert(0, 0)];
    DATA_TYPE *A2 = &A[block_convert(0, n >> 1)]; // bit shift to divide by 2
    DATA_TYPE *A3 = &A[block_convert(n >> 1, 0)];
    DATA_TYPE *A4 = &A[block_convert(n >> 1, n >> 1)];

    DATA_TYPE *B1 = &B[block_convert(0, 0)];
    DATA_TYPE *B2 = &B[block_convert(0, n >> 1)];
    DATA_TYPE *B3 = &B[block_convert(n >> 1, 0)];
    DATA_TYPE *B4 = &B[block_convert(n >> 1, n >> 1)];

    DATA_TYPE *C1 = &C[block_convert(0, 0)];
    DATA_TYPE *C2 = &C[block_convert(0, n >> 1)];
    DATA_TYPE *C3 = &C[block_convert(n >> 1, 0)];
    DATA_TYPE *C4 = &C[block_convert(n >> 1, n >> 1)];

    // recrusively call the sub-matrices for evaluation in parallel
    cilk_spawn mat_mul_par(A1, B1, C1, n >> 1);
    cilk_spawn mat_mul_par(A1, B2, C2, n >> 1);
    cilk_spawn mat_mul_par(A3, B1, C3, n >> 1);
    cilk_spawn mat_mul_par(A3, B2, C4, n >> 1);
    cilk_sync; // wait here for first round to finish

    cilk_spawn mat_mul_par(A2, B3, C1, n >> 1);
    cilk_spawn mat_mul_par(A2, B4, C2, n >> 1);
    cilk_spawn mat_mul_par(A4, B3, C3, n >> 1);
    cilk_spawn mat_mul_par(A4, B4, C4, n >> 1);
    cilk_sync; // wait here for all second round to finish
}

// recursive parallel solution to matrix multiplication - row major order
void mat_mul_par_rm(DATA_TYPE *A, DATA_TYPE *B, DATA_TYPE *C, int n, int orig_n) {
    // BASE CASE: here computation is switched to itterative matrix
    // multiplication At the base case A, B, and C point to row order matrices
    // of n x n
    if (n == base_size) {
        int i, j, k;
        for (i = 0; i < n; i++) {
            for (k = 0; k < n; k++) {
                DATA_TYPE c = 0.0;
                for (j = 0; j < n; j++) {
                    c += A[i * orig_n + j] * B[j * orig_n + k];
                }
                C[i * orig_n + k] += c;
            }
        }
        return;
    }

    // partition each matrix into 4 sub matrices
    // each sub-matrix points to the start of the z pattern
    DATA_TYPE *A1 = &A[0];
    DATA_TYPE *A2 = &A[n >> 1]; // bit shift to divide by 2
    DATA_TYPE *A3 = &A[(n * orig_n) >> 1];
    DATA_TYPE *A4 = &A[((n * orig_n) + n) >> 1];

    DATA_TYPE *B1 = &B[0];
    DATA_TYPE *B2 = &B[n >> 1];
    DATA_TYPE *B3 = &B[(n * orig_n) >> 1];
    DATA_TYPE *B4 = &B[((n * orig_n) + n) >> 1];

    DATA_TYPE *C1 = &C[0];
    DATA_TYPE *C2 = &C[n >> 1];
    DATA_TYPE *C3 = &C[(n * orig_n) >> 1];
    DATA_TYPE *C4 = &C[((n * orig_n) + n) >> 1];

    // recrusively call the sub-matrices for evaluation in parallel
    cilk_spawn mat_mul_par_rm(A1, B1, C1, n >> 1, orig_n);
    cilk_spawn mat_mul_par_rm(A1, B2, C2, n >> 1, orig_n);
    cilk_spawn mat_mul_par_rm(A3, B1, C3, n >> 1, orig_n);
    cilk_spawn mat_mul_par_rm(A3, B2, C4, n >> 1, orig_n);
    cilk_sync; // wait here for first round to finish

    cilk_spawn mat_mul_par_rm(A2, B3, C1, n >> 1, orig_n);
    cilk_spawn mat_mul_par_rm(A2, B4, C2, n >> 1, orig_n);
    cilk_spawn mat_mul_par_rm(A4, B3, C3, n >> 1, orig_n);
    cilk_spawn mat_mul_par_rm(A4, B4, C4, n >> 1, orig_n);
    cilk_sync; // wait here for all second round to finish
}

const char *specifiers[] = {"-n", "-b", "-c", "-h", 0};
int opt_types[] = {LONGARG, LONGARG, BOOLARG, BOOLARG, 0};

int main(int argc, char *argv[]) {

    base_power = 5; // default base_power value
    int n = 2048;   // default n value
    int help = 0, check = 0;

    get_options(argc, argv, specifiers, opt_types,
                &n, &base_power, &check, &help);

    if (help) {
        fprintf(stderr,
            "Usage: %s -n <n> -b <power> [-c|-h]\n"
            "\tfor input size n and basecase 2^<power>\n"
            "\tuse -c to check result against sequential MM (slow).\n"
            "\tuse -h to print this message and quit.\n", argv[0]);
        exit(0);
        exit(0);
    }
    base_size = (int)pow(2.0, (double)base_power);

    DATA_TYPE *A, *B, *C;
    A = (DATA_TYPE *)malloc(n * n * sizeof(DATA_TYPE)); // source matrix
    B = (DATA_TYPE *)malloc(n * n * sizeof(DATA_TYPE)); // source matrix
    C = (DATA_TYPE *)malloc(n * n * sizeof(DATA_TYPE)); // result matrix

    init(A, n);
    init(B, n);

    printf("Computing MM of size %d with base case %d\n", n, base_size);

    uint64_t elapsed[TIMING_COUNT];
    for (int i = 0; i < TIMING_COUNT; i++) {
        zero(C, n);
        clockmark_t begin = ktiming_getmark();
        mat_mul_par(A, B, C, n);
        clockmark_t end = ktiming_getmark();
        elapsed[i] = ktiming_diff_nsec(&begin, &end);
    }
    print_runtime(elapsed, TIMING_COUNT);

    if(check) {
        fprintf(stderr, "Check results ... \n");
        //iter result matrix
        DATA_TYPE *Cs = (DATA_TYPE *) malloc(n * n * sizeof(DATA_TYPE));
        zero(Cs, n);
        fprintf(stderr, "Computes MM sequentially ... \n");
        mat_mul_seq(A, B, Cs, n);
        if(are_equal_matrices(C, Cs, n) == 0) {
            fprintf(stderr, "Check FAILED!\n");
        } else {
            fprintf(stderr, "Check passed.\n");
        }
        free(Cs);
    }

    // clean up memory
    free(A);
    free(B);
    free(C);

    return 0;
}
