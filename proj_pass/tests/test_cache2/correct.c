/*
   This test case does a matrix multiplication in attempt to just get straight up cache misses
   */

#include "stdio.h"
#include <sys/types.h>
#include <sys/time.h>
#include <iostream>

using std::cerr; using std::cout;

// 1 << 13 is 8192
#define SIZE 1 << 13 
#define SKIP 10000

/*
 * - cache line size is 64 bytes that means that we need to skip 64 kb
 * - cache size is 32KB (32768 Bytes)
 * - 512 cache lines fit on a cache
 * - a single unsigned int is 4 bytes (8 bytes)*
 * - on a single cache line 16 (8)* unsigned ints can fit
 * - on the cache 8192 (4096)* unsigned ints can fit
 * - something like twice the size of the cache os 64KB would cause things to 
 *   be in the memory
 * - * means 64-bit architecture
 *
 * - a matrix with 20,000 cache lines would take up approximately 625MB of memory
 *   - two matrices will take up 1250MB
 * - that means 81,920,000 ints (so a 9051x9051 matrix will do)
 * - the skips in the file will have to be greater than the size of the cache 
 *   so skip 4096 ints, 5000 to be on the safe side or 10,000 to be even safer
 */

struct timeval t_start, t_end;
double t_diff;

unsigned int A[SIZE][SIZE];
unsigned int B[SIZE][SIZE];
unsigned int R[SIZE][SIZE];

int main() {
  unsigned int i, j, k;
  unsigned int bit_shift = 13;
  unsigned int matrix_size = SIZE << bit_shift;
  // fill in the matrices
  for (i = 0; i < SIZE; ++i) {
    for (j = 0; j < SIZE; ++j) {
      A[i][j] = i;
      B[i][j] = j;
      R[i][j] = i*j; 
    }
  }

  gettimeofday(&t_start,NULL);

  unsigned int sum = 0;
  unsigned int a, b, c;
/*
  for (i = 0; i < SIZE; ++i) {
    for (j = 0; j < SIZE; ++j) {
      for (k = 0; k < SIZE; ++k ) {
        

      }
    }
  } */

  unsigned int i_outer = 0;
  unsigned int j_outer = 0;
  unsigned int shift_outer = 0; // this moves the index along
  cout << "Entering loops\n";
  do {
    unsigned int cur_skip_outer = 1;
    unsigned int offset_outer = shift_outer + cur_skip_outer * SIZE;
    i_outer = offset_outer / SIZE;
    j_outer = offset_outer % SIZE;
    unsigned int cur_skip_inner = 1;
    while (offset_outer < matrix_size) {
      unsigned int shift_inner = 0;
      unsigned int i_inner = 0;
      unsigned int j_inner = 0;
      do {
        unsigned int cur_skip_inner = 1;
        unsigned int offset_inner = shift_inner + cur_skip_inner * SIZE;
        while (offset_inner < matrix_size) {
          i_inner = offset_inner / SIZE;
          j_inner = offset_inner % SIZE;
          
          for (k = 0; k < SIZE; ++k) {
            R[i_outer][j_inner] += A[i_outer][k] * B[k][j_inner]; 
          }

          cout << "\rOffset_inner is " << offset_inner;
          ++offset_inner;
        } 
      } while ((++shift_inner) % SIZE);
      ++offset_outer;
    }
    cerr << "\rProgress: " << shift_outer % SIZE;     
  } while ((++shift_outer) % SIZE);
  
  // multiply the matrices
  /*for (i = 0; i < SIZE; ++i) {
    for (j = 0; j < SIZE; ++j) {
      for (k = 0; k < SIZE; ++k) {
        sum += A[k][0] * 53 * B[k][0];
        a = 53 * sum;
        b = a * a;
        c = b * c * b*c*32423*b*c*c*c;
      }
    }
  }*/

  gettimeofday(&t_end,NULL);
  t_diff = (t_end.tv_sec - t_start.tv_sec) + (double)(t_end.tv_usec - t_start.tv_usec)/1000000;
  printf(" ---  time spent = %.6f  --- \n", t_diff);

  return 0;
}
