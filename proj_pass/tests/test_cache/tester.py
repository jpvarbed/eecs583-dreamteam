#! /usr/bin/python

import glob
import os
import re
import subprocess
import signal
import sys
import time

filename_base = "test_script"
path = "/home/pmastela/Datasets/Stock Information/NYSE"
nyse_path = "/home/pmastela/Datasets/Stock Information/NYSE/"
pig_output = "join_n.out"
max_reducers = 10
max_trials = 1
parallel_pattern = "PARALLEL"
daily_price_pattern = "DAILY_PRICE"
dividend_pattern = "DIVIDEND"
output_pattern = "OUTPUT_FILE"


class FileInformation:
  def __init__(self):
    self.price_long_to_short = {}
    self.price_short_to_long = {}
    self.divs_long_to_short = {}
    self.divs_short_to_long = {}
    self.sizes = {}

  def add(self, filename):
    short_name = self.trim(filename)
    if re.search(r"daily", filename):
      self.price_long_to_short[filename] = short_name
      self.price_short_to_long[short_name] = filename
      self.sizes[filename] = os.path.getsize(nyse_path + "daily_synthetic/" + filename) / 1048576
    else:
      self.divs_long_to_short[filename] = short_name
      self.divs_short_to_long[filename] = filename
      self.sizes[filename] = os.path.getsize(nyse_path + "dividend_synthetic/" + filename) / 1024

  def get_size(self, filename):
    return self.sizes[filename]

  def trim(self, filename):
    short_version = re.search(r"NYSE_daily_synthetic_(\w+)", filename)
    if short_version:
      return short_version.group(1)
    else:
      return re.search(r"NYSE_dividends_synthetic_(\w+)", filename).group(1)

def run_shell_program (cmd):
  try:
    retcode = subprocess.call(cmd, shell=True, executable="/bin/bash")
    if retcode != 0:
      print >> sys.stderr, "Error executing command: %s" % cmd,\
          "(return code: %s)" % retcode
  except OSError, e:
    print >> sys.stderr, "Execution failed: %s" % cmd, e

def generate_scripts_helper(template, daily_price, dividend):
  os.chdir(nyse_path + "pig_templates")
  f = open(template, "r")
  for i in range(1, max_reducers+1):
    pig_scripts_dir = nyse_path + "pig_scripts/"
    if not os.path.exists(pig_scripts_dir):
      os.makedirs(pig_scripts_dir)
    script_name = pig_scripts_dir + \
        re.search(r"NYSE_daily_synthetic_(\w+)", daily_price).group(1) + "." + \
        re.search(r"NYSE_dividends_synthetic_(\w+)", dividend).group(1) + "." + \
        (str(i) if i >= 10 else "0" + str(i)) + ".pig"
    of = open(script_name, "w")
    while True:
      line = f.readline()
      if not line:
        break
      line = re.sub(r"" + parallel_pattern, str(i), line)
      line = re.sub(r"" + daily_price_pattern, daily_price, line)
      line = re.sub(r"" + dividend_pattern, dividend, line)
      line = re.sub(r"" + output_pattern, pig_output, line)
      of.write(line)
    of.close()
    print "--- Generated: " + script_name
    f.seek(0)
  f.close()

file_info = FileInformation()

def generate_scripts():
  print "-- Generating scripts"
  # get list of daily prices and dividends
  prices_sizes = {}
  os.chdir(nyse_path + "daily_synthetic")
  for filename in glob.glob("*"):
    file_info.add(filename)

  os.chdir(nyse_path + "dividend_synthetic")
  for filename in glob.glob("*"):
    file_info.add(filename)

  templates = get_templates()
  for template in templates:
    limit = 10
    counter = 0
    for (price, div) in zip(file_info.price_long_to_short, file_info.divs_long_to_short):
      counter += 1
      if counter > limit: break
      generate_scripts_helper(template, price, div)

def get_templates():
  templates = []
  os.chdir(nyse_path + "pig_templates")
  for filename in glob.glob("template*"):
    templates.append(filename)
  return templates

def execute_scripts():
  print "-- Running scripts"
  # prep the results file
  results_path = nyse_path + "results"
  if not os.path.exists(results_path):
    os.makedirs(results_path)
  os.chdir(results_path)
  all_results = open("all_results.txt", "w")
  header_list = ["Maps", "Reduces", "MaxMapTime", "MinMapTIme", "AvgMapTime",\
      "MaxReduceTime", "MinReduceTime","AvgReduceTime","RecsWritten","MBWritten",\
      "SystemTime", "File1", "File2", "File1Size", "File2Size"]
  header = ",".join(header_list) + "\n"
  all_results.write(header)
  all_results.close()

  scripts = find_scripts()
  # write out all the results
  for script in scripts:
    print "--- Running script: " + script + ""
    for i in range(max_trials):
      print "---- Trail no. " + str(i) + " started"
      # resultdis written to <scriptname>.<run#>.result
      run_shell_program("hadoop fs -rmr " + pig_output)
      result_file_name = re.sub(r"pig", "result", script)
      os.chdir(nyse_path + "pig_scripts")
      run_shell_program("(time pig " + script + ") > " + "../results/" + result_file_name + " 2>&1")
      record_results()

def record_results():
  print "-- Recording results"
  results_dir = nyse_path + "results"
  os.chdir(results_dir)
  all_results = open("all_results.txt", "a")
  # record the results
  result_file_names = [f for f in os.listdir(results_dir) if f.endswith(".result")]
  # find the line where the results are located
  for result_file_name in result_file_names:
    print "--- Recording results from: " + result_file_name
    rf = open(result_file_name, "r")
    final_stats = []
    while True:
      line = rf.readline()
      if not line:
        break
      elif re.match("JobId", line):
        line = rf.readline()
        job_stats = re.search(r"\w+\s*(\d+)\s*(\d+)\s*(\d+)\s*(\d+)\s*(\d+)\s*(\d+)\s*(\d+)", line)
        for stat in job_stats.groups():
          final_stats.append(stat)
      elif re.match(r"Total records written : (\d+)", line):
        job_stats = re.search(r"Total records written : (\d+)", line)
        final_stats.append(job_stats.group(1))
      elif re.search(r"Total bytes written : (\d+)", line):
        job_stats = re.search(r"Total bytes written : (\d+)", line)
        # convert from megabytes to bytes
        mbs = int(job_stats.group(1))/1048576
        final_stats.append(str(mbs))
      elif re.match(r"real\s*(.*s)", line):
        job_stats = re.match(r"real\s*(.*s)", line)
        final_stats.append(job_stats.group(1))
    filenames = re.search(r"(\w+)\.(\w+)", result_file_name)
    file1 = filenames.group(1)
    final_stats.append(file1)
    final_stats.append(str(file_info.get_size("NYSE_daily_synthetic_" + file1)) + "M")

    file2 = filenames.group(2)
    final_stats.append(file2)
    final_stats.append(str(file_info.get_size("NYSE_dividends_synthetic_" + file2)) + "K")

    all_results.write(",".join(final_stats) + "\n")
    rf.close()
    run_shell_program("rm -f " + result_file_name)
  all_results.close()

# finds the scripts to run
def find_scripts():
  script_files = []
  os.chdir(nyse_path + "pig_scripts")
  for filename in glob.glob("*.pig"):
    script_files.append(filename)
  return script_files

if __name__=='__main__':
  print "- Starting script"
  start = time.time()
  generate_scripts()
  execute_scripts()
  record_results()
  print "- Completed script in " + str(round(time.time() - start, 2)) + " seconds\n"

