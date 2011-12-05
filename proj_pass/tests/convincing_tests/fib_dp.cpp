#include <iostream>
#include <cmath>
using namespace std;

void Fibonacci(const int n)
{
  int *fib = new int[n]; // allocate this dynamically! :)
  for(int i = 2; i < n; i++) {
    fib[0]=0;
    fib[1]=1;
    fib[i]=fib[i-1]+fib[i-2];
  }

  // print them all out
  for(int i = 0; i < n; i++) {
    cout << fib[i] << ' ';
  }

  delete[] fib;
}

int main(int argc, char* argv[])
{
  Fibonacci(20);
  return 0;
}

