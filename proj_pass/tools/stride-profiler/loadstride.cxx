/*
  Each load should have an instance and tell the address it loads.
  Then this can be used to calculate the top stride values and stride differences...
*/

#include <iostream>
#include <vector>
#include <utility>
#include <map>
#include <algorithm>

#include "loadstride.hxx"

using namespace std;

unsigned int LoadStride::TOPCOUNT = 4; // number of top stride values to return (not including zero)

LoadStride::LoadStride(uint32_t load_id, int32_t exec_count) {
  loadID = load_id;
  executionCount = exec_count;
  strideZeroCount = 0;
  strideZeroDifferenceCount = 0;
  topStrideHolder = make_pair(-1, -1);
  lastAddress = -1;

  cout << "looking at load_id: "<<load_id<<"\n";
  //cout << "exec_count <" << exec_count << "> " << "profile <" << profileN << "> skip<" << skipN << ">\n";
}

LoadStride::~LoadStride() {
}

void LoadStride::updateTopStrideValues(long value) {
  long count = strideValuesToCount[value];
  if (count > topStrideHolder.second) {
    topStrideHolder.first = value;
    topStrideHolder.second = count;
  }
  if (value == 0) {
    return; // don't keep track of 0 for the top stride values
  }
  for (int i = 0; i < topStrideValues.size(); i++) {
    if (topStrideValues[i].first == value) {
      topStrideValues[i].second = count;
      return;
    }
  }
  if (topStrideValues.size() < LoadStride::TOPCOUNT) {
    topStrideValues.push_back(make_pair(value, count));
    return;
  }
  map<long, long>::iterator itStart, itEnd;
  long lowest_found = (topStrideValues.begin())->second;
  int location = 0; 
  for (int i = 1; i < topStrideValues.size(); i++) {
    if (topStrideValues[i].second < lowest_found) {
      lowest_found = topStrideValues[i].second;
      location = i;
    }
  }
  if (count > lowest_found) {
    topStrideValues[location] = make_pair(value, count);
  }
}

void LoadStride::addAddress(uint64_t addr) {
  if (lastAddress == -1) {
    lastAddress = addr;
    return;
  }
  
  unsigned long last_address = lastAddress;
  lastAddress = addr;

  long stride = addr - last_address;
  if (stride == 0 || isSameValue(addr, last_address)) {
    strideZeroCount++;
    return;
  }

  if (strideValues.size() == 0) {
    strideValues.push_back(stride);
    if (strideValuesToCount.count(stride)) {
      strideValuesToCount[stride]++;
    } else {
      strideValuesToCount[stride] = 1;
    }
    updateTopStrideValues(stride);
    return;
  }

  long last_stride = strideValues.back();
  strideValues.push_back(stride);
  if (strideValuesToCount.count(stride)) {
    strideValuesToCount[stride]++;
  } else {
    strideValuesToCount[stride] = 1;
  }
  updateTopStrideValues(stride);

  long strideDifference = stride - last_stride;
  if (strideDifference == 0) {
    strideZeroDifferenceCount++;
  }
}


