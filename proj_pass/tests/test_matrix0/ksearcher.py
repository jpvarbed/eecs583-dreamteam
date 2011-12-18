#!/usr/bin/python

import sys
import os
import re
import subprocess
import shutil
import time
import string
import operator

COMMAND = 'make s=300'
MAX_TRIALS = 3
INITIAL_K = 100

# used for finding the optimal k value
K_PARTITIONS = 5
CONSECUTIVE = 3

PROJ_PATH = '' # will be initialized by the user
PREFETCH_FILE = '/lib/projpass/strideprefetch.cpp'

RES_FILE = None

class Result:
  def __init__(self, k, unmodified_time, modified_time):
    self.k = k
    self.unmodified_time = string.atof(unmodified_time)
    self.modified_time = string.atof(modified_time)
    print unmodified_time + ' ' + modified_time + ' '
    self.performance = (self.unmodified_time - self.modified_time) / self.unmodified_time

  def __str__(self):
    return str(self.k) + ' %.6f ' % self.unmodified_time + '%.6f ' % self.modified_time +\
        '%.6f' % self.performance

class Results:
  def __init__(self):
    self.results = {}
    self.avg_performance = {}

  def __getitem__(self, key):
    if key in self.results:
      return self.results[key]
    else:
      return None

  # parse the input and add a new Result to results
  def add(self, k, result):
    print '---- Add ' + str(result)
    if k not in self.results:
      self.results[k] = []
      self.avg_performance[k] = result

    # update the performance
    self.avg_performance[k].performance = \
        (self.avg_performance[k].performance * len(self.results[k]) + result.performance) / \
        (len(self.results[k]) + 1)
    self.results[k].append(result)
    print '---- Done adding'

  # returns the average performance for k
  def get_avg_perf(self, k):
    print '---- In get_avg_perf'

    return self.avg_performance[k].performance

    avg_perf = 0
    for result in self.results[k]:
      print '----- ' + str(result)
      avg_perf = avg_perf + result.performance
    avg_perf = avg_perf / len(self.results[k])

    return avg_perf

  def get_performance_rankings(self):
    sorted_list = []
    for value in self.avg_performance.values():
      print '---- Appending ' + str(value)
      sorted_list.append(value)

    for elt in sorted_list:
      print elt

    sorted_list.sort(key = operator.attrgetter('performance'), reverse = True)
    return sorted_list

class Parser:
  def get_result(self, k, output):
    unmodified_time = None
    modified_time = None
    for line in output:
      time_spent = re.match(r' ---  time spent = (\d+\.\d+)', line)
      if time_spent is None:
        continue

      if modified_time is None:
        modified_time = time_spent.group(1)
      else:
        unmodified_time = time_spent.group(1)

    return Result(k, unmodified_time, modified_time)

def bin_search(a, x, lo = 1, hi = None):
  if hi is None:
    hi = len(a)
  while lo < hi:
    mid = (lo + hi)
    midval = a[mid]

    if midval < x:
      lo = mid + 1
    elif midval > x:
      hi = mid
    else:
      return mid
  return -1

# makes a new range that consists of K_PARTITIONS points
def make_range(lo, hi):
  r = []
  partition = (int) ((hi - lo) / K_PARTITIONS)
  if partition is not 0:
    for i in range(lo, hi+1):
      if i % partition is 0:
        r.append(i + 1) # +1 because low could be 0 and we don't want that

  return r

# find the best range with
def get_best_range_begin(k_range, results):
  range_start = None
  for i in range(len(k_range) - CONSECUTIVE + 1):
    print '--- Getting performance for ' + str(k_range[i]) +\
          ' ' + str(k_range[i+1]) + ' ' + str(k_range[i+2])
    print '--- average performance for ' + str(k_range[i]) + ' is ' +\
        str(results.get_avg_perf(k_range[i]))
    cand_perf = results.get_avg_perf(k_range[i]) +\
                results.get_avg_perf(k_range[i+1]) +\
                results.get_avg_perf(k_range[i+2])
    if range_start is None or cand_perf > range_start:
      range_start = i

  return range_start

# split the range to 5 points to examine
# run int then on those 5 points
# pick the 3 that have greatest consecutive perf
# continue doing this until
def bin_find_optimal_k(lo = 0, hi = None):
  results = Results()
  if hi is None:
    hi = INITIAL_K
  while lo < hi:
    k_range = make_range(lo, hi)
    if not k_range:
      break

    print '--- Calculating performance for: ' + ','.join(map(str,k_range))
    for k in k_range:
      calc_perf(k, results)
    # pick a new low and high
    range_begin = get_best_range_begin(k_range, results)
    lo = k_range[range_begin]
    hi = k_range[range_begin + CONSECUTIVE - 1]

  slist = results.get_performance_rankings()
  print '\n'.join(map(str, slist))

  return hi

def modify_file(cur_k):
  print '--- Modifying the file'
  assert os.path.exists(PREFETCH_FILE) == True

  f = open(PREFETCH_FILE, 'r+')
  text = f.read()
  # couldn't get it to work with this way
  #text = re.sub(r'^\s', 'K = ' + str(cur_k) + ';', text)
  text = re.sub('CHANGE_K_FLAG', str(cur_k), text)
  f.seek(0)
  f.write(text)
  f.truncate()
  f.seek(0)
  print f.read()
  f.flush()
  f.close()

# calculates the performance with the new k
def calc_perf(k, results):
  print '-- Finding optimal K'
  modify_file(k)

  os.chdir(PROJ_PATH)
  f = open(PREFETCH_FILE, 'r')
  print f.read()
  f.close()
  subprocess.call('./domake', shell=True)

  os.chdir(PROJ_PATH + '/tests/test_matrix0')
  cmd = COMMAND

  parser = Parser()
  # loop max trials times making sure that a negative number was reported
  # when in actuality the performance was better
  for i in range(MAX_TRIALS):
    print '--- Executing "' + cmd + '" with K = ' + str(k)
    proc = subprocess.Popen(cmd, shell=True, stdin=subprocess.PIPE, \
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, close_fds=True)
    #result = proc.communicate()[0]
    result = parser.get_result(k, proc.stdout)
    print '---- Adding result ' + str(result)
    results.add(k, result)
    print '---- about to assert ' + (str(len(results[k])))
    print '---- done asserting and about to write '
    RES_FILE.write(time.ctime(time.time()) + '\t' + str(k) + '\t' +
        '%.6f' % result.unmodified_time + '\t' +
        '%.6f' % result.modified_time + '\t' +
        '%.6f' % result.performance + '\n')
    RES_FILE.flush()
    print '--- Done executing "' + cmd + '"'

  return results.get_avg_perf(k)

if __name__ == '__main__':
  res_file_name = 'results.txt'
  if not os.path.exists(res_file_name):
    RES_FILE = open(res_file_name, 'a')
    RES_FILE.write('Timestamp\t\t\tk\tNo Prefetch\tPrefetch\tPerformance (+/-)\n')
  else:
    RES_FILE = open(res_file_name, 'a')

  try:
    # back up the file
    if len(sys.argv) == 1:
      print 'Enter the path to your proj_pass directory.'
      sys.exit()

    PROJ_PATH = sys.argv[1]
    PREFETCH_FILE = PROJ_PATH + PREFETCH_FILE
    print 'The file that will be modified is ' + PREFETCH_FILE
    shutil.copyfile(PREFETCH_FILE, PREFETCH_FILE + '.bak')
    bin_find_optimal_k()
  finally:
    # get the file to its original shape
    print '-- In the finally clause'
    if len(PROJ_PATH) != 0:
      shutil.copyfile(PREFETCH_FILE + '.bak', PREFETCH_FILE)
    RES_FILE.close()

