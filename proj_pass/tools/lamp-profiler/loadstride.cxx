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

  number_skipped = 0;
  number_profiled = 0;
  profileN = min(1/5 * exec_count, 1000);
  skipN = (exec_count - profileN) < 0 ? 0 : (exec_count - profileN);
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

void LoadStride::updateTopStrideDifferenceValues(long value) {
  long count = strideDifferencesToCount[value];
  for (int i = 0; i < topStrideDifferenceValues.size(); i++) {
    if (topStrideDifferenceValues[i].first == value) {
      topStrideDifferenceValues[i].second = count;
      return;
    }
  }
  if (topStrideDifferenceValues.size() < LoadStride::TOPCOUNT) {
    topStrideDifferenceValues.push_back(make_pair(value, count));
    return;
  }
  map<long, long>::iterator itStart, itEnd;
  long lowest_found = (topStrideDifferenceValues.begin())->second;
  int location = 0; 
  for (int i = 1; i < topStrideDifferenceValues.size(); i++) {
    if (topStrideDifferenceValues[i].second < lowest_found) {
      lowest_found = topStrideDifferenceValues[i].second;
      location = i;
    }
  }
  if (count > lowest_found) {
    topStrideDifferenceValues[location] = make_pair(value, count);
  }
}

void LoadStride::addAddress(uint64_t addr) {
  if (number_skipped < skipN) {
    number_skipped++;
    return;
  } else if (number_profiled == profileN) {
    number_profiled = 0;
    number_skipped = 0;
    clearAddresses();
    return;
  }

  number_profiled++;

  if (number_profiled == 1) {
    addresses.push_back(addr);
    return;
  }
  
  unsigned long last_address = addresses.back();
  addresses.push_back(addr);
  
  long stride = addr - last_address;
  if (stride == 0) {
    strideZeroCount++;
  }

  if (number_profiled == 2) {
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
  strideDifferences.push_back(strideDifference);
  if (strideDifferencesToCount.count(strideDifference)) {
    strideDifferencesToCount[strideDifference]++;
  } else {
    strideDifferencesToCount[strideDifference] = 1;
  }
  updateTopStrideDifferenceValues(strideDifference);
}


