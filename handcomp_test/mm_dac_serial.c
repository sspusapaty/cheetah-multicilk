/**
 * Copyright (c) 2015 MIT License by 6.172 Staff
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 **/

// Matrices are laid out in row-major order.
// For example,  if C is an N by M matrix, then
//               C_{1,1} is stored at C[0]
//               C_{1,2} is stored at C[1]
//               C_{2,1} is stored at C[M]
// and in general C_{i,j} is stored at C[ (i-1)*M + j-1 ].
//
// Most matrix multiplication routines are written on column-major
// order to be compatible with FORTRAN.  This one is in row-major
// order, which is compatible with C and C++ arrays.
//
// In C, arrays are indexed from 0 to N^2-1.
// In Math, matrices are indexed from 1 to N in each axis.
//
// This code works even if the matrices are not a power of 2 in size.
// This code could be adapted to work for non-square matrices.

#include <cilk/cilk.h>

#include <stdbool.h> 
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "ktiming.h"


// Uncomment this line to use loops for the small matrix base case
#define USE_LOOPS_FOR_SMALL_MATRICES

#ifndef TIMES_TO_RUN
#define TIMES_TO_RUN 1 
#endif

unsigned int randomSeed = 1;

// mm_loop_serial is the standard triply nested loop implementation.
//   C += A*B
// n is the matrix size.  Note that C is not set to zero -- its initial value
// is added to the matrix-product of B and C

void mm_loop_serial(int *C, const int *A, const int *B, int n) {
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++)
      for (int k = 0; k < n; k++)
        C[i*n+j] += A[i*n+k] * B[k*n+j];
}

// mm_internal is the recursive implementation of matrix multiply.
// This code will not work on non-square matrices.
void mm_internal(int *C, const int *A, const int *B, int n, int length) {
  // Effect: Compute C+=A*B,
  // where C, A, and B are the starting addresses of submatrices of n-by-n
  // matrices, each with dimension length x length.
  // The rows of C, A, and B, each contain n elements.
  static const int threshold = 16;

  // Base cases
  if (length == 0) {
    return;
  } else if (length == 1) {
    C[0] += A[0] * B[0];
#ifdef USE_LOOPS_FOR_SMALL_MATRICES
  } else if (length < threshold) {
    // Use a loop for small matrices
    for (int i = 0; i < length; i++)
      for (int j = 0; j < length; j++)
        for (int k = 0; k < length; k++)
          C[i*n+j] += A[i*n+k] * B[k*n+j];
#endif  // USE_LOOPS_FOR_SMALL_MATRICES
  } else {
    // Partition the matrices
    int mid = length / 2;

    int *C11 = C;
    int *C12 = C + mid;
    int *C21 = C + n*mid;
    int *C22 = C + n*mid + mid;

    int const *A11 = A;
    int const *A12 = A + mid;
    int const *A21 = A + n*mid;
    int const *A22 = A + n*mid + mid;

    int const *B11 = B;
    int const *B12 = B + mid;
    int const *B21 = B + n*mid;
    int const *B22 = B + n*mid + mid;

    mm_internal(C11, A11, B11, n, mid);
    mm_internal(C12, A11, B12, n, mid);
    mm_internal(C21, A21, B11, n, mid);
    mm_internal(C22, A21, B12, n, mid);

    mm_internal(C11, A12, B21, n, mid);
    mm_internal(C12, A12, B22, n, mid);
    mm_internal(C21, A22, B21, n, mid);
    mm_internal(C22, A22, B22, n, mid);
  }
}

// This is the public interface to mm_internal.
void mm_recursive_parallel(int *C, const int *A, const int *B, int n) {
  // Effect:  C, A, and B are n*n matrices of Ts.
  // Perform  C += A * B.
  mm_internal(C, A, B, n, n);
}


// Here are some tests.

int test_status = 0;

void copy_matrix(int *dest, const int *src, int n) {
  for (int i = 0; i < n*n; ++i)
    dest[i] = src[i];
}

bool are_equal_matrices(const int *a, const int *b, int n) {
  for (int i = 0; i < n*n; ++i)
    if (a[i] != b[i])
      return false;
  return true;
}

// Test to see if mm_recursive_parallel and mm_loop_serial do the same thing.
void test_mm(const int *C, const int *A, const int *B, int n,  const char *test_name) {
  clockmark_t begin, end;
  uint64_t running_time[TIMES_TO_RUN];
  int * Cp;
  int * Cs;

  Cp = (int*) malloc(sizeof(int) * (n*n));
  Cs = (int*) malloc(sizeof(int) * (n*n));
  copy_matrix(Cp, C, n);
  copy_matrix(Cs, C, n);
  
  printf("%s:\n", test_name);
  
  for(int i = 0; i < TIMES_TO_RUN; i++) {
    begin = ktiming_getmark();
    mm_recursive_parallel(Cp, A, B, n);
    end = ktiming_getmark();
    running_time[i] = ktiming_diff_usec(&begin, &end);
    
    mm_loop_serial(Cs, A, B, n);
    if (!are_equal_matrices(Cp, Cs, n)) {
      printf(">>>>> %s failed \n", test_name);
      ++test_status;
    }
  }

  if( TIMES_TO_RUN > 10 ) 
    print_runtime_summary(running_time, TIMES_TO_RUN); 
  else 
    print_runtime(running_time, TIMES_TO_RUN);
  
  free(Cs);
  free(Cp);
}


// Do a simple check and make sure it is right.
void smoke_test1() {
  // Second element of each array is never used and should not change
  int C[] = {1, -1};
  int A[] = {5, -1};
  int B[] = {7, -1};

  mm_recursive_parallel(C, A, B, 1);

  if (C[0] != 36 || C[1] != -1
      || A[0] != 5  || A[1] != -1
      || B[0] != 7  || B[1] != -1) {
    printf (">>>>> smoke_test1 failed <<<<< \n");
    ++test_status;
  }
}

// Another simple check.
void smoke_test2() {
  // last element of each array is never used and should not change
  int c[] = {5, 6, 7, 8, -1};
  int a[] = {9, 10, 11, 12, -1};
  int b[] = {13, 14, 15, 16, -1};

  mm_recursive_parallel(c, a, b, 2);

  if (c[4] != -1|| a[4] != -1|| b[4] != -1
      || c[0] != 5+ 9*13+10*15 || c[1] != 6+ 9*14+10*16
      || c[2] != 7+11*13+12*15 || c[3] != 8+11*14+12*16) {
    printf (">>>>> smoke_test2 failed <<<<< \n");
    ++test_status;
  }
}

// A bigger test.
void random_test(int n, const char *test_name) {
  int *a = (int *) malloc(sizeof(int)*(n*n));
  int *b = (int *) malloc(sizeof(int)*(n*n));
  int *c = (int *) malloc(sizeof(int)*(n*n));

  for (int i = 0; i < n*n; i++) {
    // Range of Random ints chosen to avoid roundoff error.
    c[i] = rand_r(&randomSeed) & 0xff;
    a[i] = rand_r(&randomSeed) & 0xff;
    b[i] = rand_r(&randomSeed) & 0xff;
  }

  test_mm(c, a, b, n, test_name);

  free(c);
  free(b);
  free(a);
}

// return true iff n = 2^k.
bool is_power_of_2(int n) {
  bool match = false;  // whether a bit has been matched.
  int field = 0x1;     // field for bit matching.
  for (int i = sizeof(field) * 8; i > 0; --i, field <<= 1) {
    if (field & n) {
      if (match) return false;
      match = true;
    }
  }
  return match;
}

// The arguments to mm:
//   --verify    run the verification tests.
//   --notime    don't measure run times.
//   --pause     pause at the end of the run
//   N           (a number) run a matrix multiply on an
//               NxN matrix, without checking that the
//               answer is correct.  If N is absent, run a
//               a standard test suite.
int main(int argc, char *argv[]) {
  int N = -1;
  bool do_verify = false, do_time = true, do_pause = false;
  const char *N_str = NULL;
  
  if(argc != 2) {
    fprintf(stderr, "Usage: fib [<cilk-options>] <n>\n");
    exit(1);
  }

  N = atoi(argv[1]);
  N_str = argv[1];
  if (!is_power_of_2(N)) {
    fprintf(stderr, "N must be a power of 2 \n");
    exit(1);
  }
  
  // Smoke tests.
  smoke_test1();
  smoke_test2();
  /*
  random_test(4, "smoke_test4");
  random_test(64, "smoke_test64");
  */
  
  char test_name[25] = "matrix";
  strncat(test_name, N_str, 12);
  random_test(N, test_name);

  printf ("Test %s \n", test_status ? "failed" : "Succeeded");

  return test_status;
}
