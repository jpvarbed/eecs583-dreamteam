# Generates a file with MAX_NUMS random integers. 1 integer per line.

import random
import time

MAX_NUMS = 1000000 # 1 million

def generate_nums():
  f = open('unsorted_nums.txt', 'w')
  for i in range(MAX_NUMS):
    f.write(str(random.randint(0, MAX_NUMS)) + '\n')
  f.close()

if __name__=='__main__':
  print '- Generating ' + str(MAX_NUMS) + ' numbers'
  start = time.time()
  generate_nums()
  print '- Completed generating numbers in ' + str(round(time.time() - start, 2)) + ' seconds'

