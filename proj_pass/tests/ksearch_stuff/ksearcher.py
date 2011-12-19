#!/usr/bin/python

import operator
import os
import re
import shutil
import subprocess
import sys
import string
import time


MSIZE = '300'
COMMAND = 'make s=' + MSIZE
MAX_TRIALS = 5
INITIAL_K = 20

# used for finding the optimal k value
K_PARTITIONS = 20
CONSECUTIVE = 3

PROJ_PATH = '' # will be initialized by the user
PREFETCH_FILE = '/lib/projpass/strideprefetch.cpp'
TEST_PATH = '/tests/test_matrix1'
ORIG_PREFETCH_FILE = ''
RES_FILE = None

class Result:
  """
  Result stores the important details from a run,

  i.e., K, the prefetching distance; time to complete without prefetching,
  and time with prefetchin

  """

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
  """
  Keeps track of the Results instances. Keeps track of the average performance

  """
  def __init__(self):
    self.results = {}
    self.best_performance = {}

  def __getitem__(self, key):
    if key in self.results:
      return self.results[key]
    else:
      return None

  # parse the input and add a new Result to results
  def add(self, k, result):
    """
    Records result and K

    """
    print '---- Add ' + str(result)
    if k not in self.results:
      self.results[k] = []
      self.best_performance[k] = result

    # update the performance
    if result.performance > self.best_performance[k].performance:
      self.best_performance[k].performance = result.performance

    self.results[k].append(result)
    print '---- Done adding'

  # returns the average performance for k
  def get_best_perf(self, k):
    """
    Returns the average performance for results with K set to k

    """
    return self.best_performance[k].performance

  def get_performance_rankings(self):
    """
    Returns a sorted list of the results' performance

    """
    sorted_list = []
    for value in self.best_performance.values():
      print '---- Appending ' + str(value)
      sorted_list.append(value)

    sorted_list.sort(key = operator.attrgetter('performance'), reverse = True)
    return sorted_list

class Parser:
  """
  Extracts the important content regarding results such as k and the times
  with and without prefetching

  """
  def get_result(self, k, output):
    """
    Extracts the time it took to run with and without prefetching
    Returns a Result with the information

    """
    unmodified_time = None
    modified_time = None
    for line in output:
      print line
      time_spent = re.match(r' ---  time spent = (\d+\.\d+)', line)
      if time_spent is None:
        continue

      if unmodified_time is None:
        unmodified_time = time_spent.group(1)
      else:
        modified_time = time_spent.group(1)

    return Result(k, unmodified_time, modified_time)

def make_range(lo, hi):
  """
  Returns a new ranged based on hi and lo and K_PARTITIONS.

  """
  r = []
  partition = (int) ((hi - lo) / K_PARTITIONS)
  if partition is not 0:
    for i in range(lo, hi+1, partition):
        r.append(i) # +1 because low could be 0 and we don't want that

  print ','.join(map(str,r))
  return r

def get_best_range_begin(k_range, results):
  """
  Optimization algorithm that selects a range that saw most significant
  performance gain

  """
  range_start = None
  max_perf = -sys.maxint-1 # the minimum value
  for i in range(len(k_range) - CONSECUTIVE + 1):
    print '--- Getting performance for ' + str(k_range[i]) +\
          ' ' + str(k_range[i+1]) + ' ' + str(k_range[i+2])
    print '--- average performance for ' + str(k_range[i]) + ' is ' +\
        str(results.get_best_perf(k_range[i]))
    cand_perf = results.get_best_perf(k_range[i]) +\
                results.get_best_perf(k_range[i+1]) +\
                results.get_best_perf(k_range[i+2])
    print '--- Cand is ' + str(cand_perf) + ' Max is ' + str(max_perf)
    if cand_perf > max_perf:
      max_perf = cand_perf
      range_start = i
  print '0000000000000000000000000000000000000000000'
  print '---- range_start is ' + str(range_start)

  return range_start

def bin_find_optimal_k(lo = 0, hi = None):
  """
  Splits the range into K_PARTITIONS
  Finds the optimal K between lo and hi

  """
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

  # save the rankings to a file
  slist = results.get_performance_rankings()
  f = open('result' + str(int(time.time())) + '-' + MSIZE + '.txt', 'w')
  f.write('K no_pref pref perf\n')
  f.write('\n'.join(map(str, slist)))
  f.close()

  return hi

def modify_file(cur_k):
  """
  Changes the K value in the PREFETCH_FILE

  """
  print '--- Modifying the file'
  assert os.path.exists(ORIG_PREFETCH_FILE) == True
  rf = open(ORIG_PREFETCH_FILE, 'r')
  text = rf.read()
  rf.close()
  assert re.search('CHANGE_K_FLAG', text) is not None
  text = re.sub('//#define K_SEARCH', '#define K_SEARCH', text)
  text = re.sub('CHANGE_K_FLAG', str(cur_k), text)
  f = open(PREFETCH_FILE, 'w')
  f.write(text)
  f.close()
  assert f.closed is True
  print '--- Done modifying the file'

# calculates the performance with the new k
def calc_perf(k, results):
  print '-- Finding optimal K'
  modify_file(k)
  os.chdir(PROJ_PATH)
  # need to remake this every time because of the modified K value
  print 'want to be in ' + PROJ_PATH + ' is in ' + os.getcwd()
  subprocess.call('./domake', shell=True)
  print 'tried do making ' + os.getcwd()

  os.chdir(PROJ_PATH + TEST_PATH)

  # profiling only needs to happen once (it's quite an expensive call)
  print '-- first we profile'
  cmd = COMMAND + ' profile'
  subprocess.call(cmd, shell=True)

  parser = Parser()
  # loop max trials times making sure that a negative number was reported
  # when in actuality the performance was better
  for i in range(MAX_TRIALS):
    cmd = COMMAND + ' prefetch'
    print '-- then we prefetch'
    subprocess.call(cmd, shell=True)
    print '-- and then we diff and get result'
    cmd = COMMAND + ' diff'
    print '--- Executing "' + cmd + '" with K = ' + str(k)
    proc = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, \
        stderr=subprocess.STDOUT, close_fds=True)
    result = parser.get_result(k, proc.stdout)

    print '---- Adding result ' + str(result)
    results.add(k, result)
    print '---- about to assert ' + (str(len(results[k])))
    print '---- done asserting and about to write '
    RES_FILE.write(time.ctime(time.time()) + '\t' + str(k) + '\t' + MSIZE +'\t' +
        '%.6f' % result.unmodified_time + '\t' +
        '%.6f' % result.modified_time + '\t' +
        '%.6f' % result.performance + '\n')
    RES_FILE.flush()
    print '--- Done executing "' + cmd + '"'

  return results.get_best_perf(k)

if __name__ == '__main__':
  print '- Running KSearcher'
  res_file_name = 'results.txt'
  if not os.path.exists(res_file_name):
    RES_FILE = open(res_file_name, 'a')
    RES_FILE.write('Timestamp\t\t\tk\tsize\tNo Prefetch\tPrefetch\tPerformance (+/-)\n')
  else:
    RES_FILE = open(res_file_name, 'a')

  try:
    # back up the file
    if len(sys.argv) == 1:
      print 'Enter the path to your proj_pass directory.'
      sys.exit()

    PROJ_PATH = sys.argv[1]
    PREFETCH_FILE = PROJ_PATH + PREFETCH_FILE
    ORIG_PREFETCH_FILE = PREFETCH_FILE + '.orig'
    print '-The file that will be modified is ' + PREFETCH_FILE
    shutil.copyfile(PREFETCH_FILE, ORIG_PREFETCH_FILE)
    bin_find_optimal_k()
  finally:
    # get the file to its original shape
    print '-- In the finally clause'
    if len(PROJ_PATH) != 0:
      shutil.copyfile(ORIG_PREFETCH_FILE, PREFETCH_FILE)
    RES_FILE.close()

