/*
  This test case does a matrix multiplication in attempt to just get straight up cache misses

*/

#include "stdlib.h"
#include "stdio.h"
#include <sys/types.h>
#include <sys/time.h>

struct timeval t_start, t_end;
double t_diff;


  int i, j, k;
int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("Excuse me kind sir, but could you ever so kindly provide a size. I sincerely thank you.\n");
    return 0;
  }
  int h = 0;
  int SIZE = atoi(argv[1]);

  /*int A[SIZE][SIZE];
  int B[SIZE][SIZE];
  */
  int **A = NULL, **B = NULL;
  A = (int**)malloc(SIZE * sizeof(int*));
  B = (int**)malloc(SIZE * sizeof(int*));
  for(h = 0; h < SIZE; h++){
    A[h] = (int*)malloc(SIZE *sizeof(int));
    B[h] = (int*)malloc(SIZE *sizeof(int));
  }
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
        sum += A[k][0] * 53 * B[k][0];
        int a = 53 * sum;
        int b = a * a;
        int c = b * c * b*c*32423*b*c*c*c;
      }
    }
  }

  gettimeofday(&t_end,NULL);
  t_diff = (t_end.tv_sec - t_start.tv_sec) + (double)(t_end.tv_usec - t_start.tv_usec)/1000000;
  printf(" ---  time spent = %.6f  --- \n", t_diff);

  for(h = 0; h < SIZE; h++){
    free(A[h]);
    free(B[h]);
  }
  free(A);
  free(B);

  return 0;
}
