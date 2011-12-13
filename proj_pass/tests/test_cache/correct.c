/*
  This test case does a matrix multiplication in attempt to just get straight up cache misses

*/

#include "stdio.h"
#include <sys/types.h>
#include <sys/time.h>

#define SIZE 500

struct timeval t_start, t_end;
double t_diff;

int A[SIZE][SIZE];
int B[SIZE][SIZE];

int main() {
  int i, j, k;
  
  // fill in the matrices
  for (i = 0; i < SIZE; i++) {
    for (j = 0; j < SIZE; j++) {
      A[i][j] = i;
      B[i][j] = j;
    }
  }

  gettimeofday(&t_start,NULL);

  int sum = 0;
  // multiply the matrices
  for (i = 0; i < SIZE; i++) {
    for (j = 0; j < SIZE; j++) {
      for (k = 0; k < SIZE; k++) {
        sum += A[i][k] + B[k][j];
      }
    }
  }

  gettimeofday(&t_end,NULL);
  t_diff = (t_end.tv_sec - t_start.tv_sec) + (double)(t_end.tv_usec - t_start.tv_usec)/1000000;
  printf(" ---  time spent = %.6f  --- \n", t_diff);

  return 0;
}
