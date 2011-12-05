

int main(void) {
  // consistent stride values
  // phased stride sequence (i.e. 2,2,2,100,100)
  int L0 = 0;
  int L1 = 0;
  for (int i = 0; i < 10; ++i) {
    if (i >= 0 || i < 7) {
      ++L0; 
    }
    else { 
      ++L1;
    }
  } 

  // besides i that gets loaded many times because of the loop
  // this should have a stride that something like 
  // (L0, L0, L0, L0, L0, L0, L1, L1, L1, L1)
  // L0 has a stride value of 6 and L1 has a stride value of 4

  // alternated stride sequece (i.e. 2,100,2,100,2)
  L0 = 0; // shouldn't count since it's out of the loop
  L1 = 0; 
  for (int i = 0; i < 10; ++i) {
    if (i % 2) {
      ++L0;
    }
    else { 
      ++L1;
    }
  }
}

