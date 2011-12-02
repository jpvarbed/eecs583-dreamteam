#include <stdio.h>

int main(){
    int a[100];
    int z;
    for(z=0;z<100; z++)
        a[z] = 1;
    int offset;
    int p;
    for(z=1;z<50;z++) {
      offset = -5;
      if (z % 2) offset = 5;
      p = a[50 + offset];
    }
    return 0;
}
