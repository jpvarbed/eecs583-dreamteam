#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>

struct timeval t_start, t_end;
double t_diff;

int main(){
  int a[100000];
  
  int i;
  for (i = 0; i<100000;i++) {
    a[i] = i*2 + i-3;
  }

  gettimeofday(&t_start,NULL);
  int b=0, j = 0;
  for (i = 0; i<100000; i += 50) {
      b += a[i+60];
      j += b;
      j *= 2;
      j--;
      j++;
      j = j*2 / 3 + 5 -2 *5;
  }

  printf("done");
 
  gettimeofday(&t_end, NULL);
  t_diff = (t_end.tv_sec - t_start.tv_sec) + (double)(t_end.tv_usec - t_start.tv_usec)/1000000;
  printf(" ---  time spent = %.6f  --- \n", t_diff);
  
  return 0;
}
