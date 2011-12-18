#ifndef LOADSTRIDE_H
#define LOADSTRIDE_H
#include <vector>
#include <utility>
#include <map>
#include <stdint.h>

using namespace std;
class LoadStride {

  public:
    LoadStride(uint32_t, int32_t);
    ~LoadStride();

    void addAddress(uint64_t);
    
    unsigned long getStrideZeroCount() {
      return strideZeroCount;
    }
    
    unsigned long getStrideZeroDifferenceCount() {
      return strideZeroDifferenceCount;
    }

    vector< pair<long, long> > *getTopStrideValues() {
      return &topStrideValues;
    }

    pair<long, long> *getTopStrideValue() {
      return &topStrideHolder; // pair of <value, count>
    }

    vector< pair<long, long> > *getTopStrideDifferenceValues() {
      return &topStrideDifferenceValues;
    }
 
    uint32_t getLoadID() {
      return loadID;
    }

    unsigned int getNumberUniqueStrides() {
      return strideValuesToCount.size();
    }

    unsigned int getStrideExecCount() {
      return strideValues.size();
    }

    void clearAddresses() {
      lastAddress = -1;
    }

    bool isSameValue(long value1, long value2) {
      // value1 and value2 are treated to be the same vlaue
      // if they only are different in the last 4 bits
      return (value1 >> 4 == value2 >> 4);
    }

  private:
    static unsigned int TOPCOUNT;

    uint32_t loadID;
    uint32_t executionCount;
    long lastAddress;

    vector<long> strideValues;
    vector<long> strideDifferences;
    map<long, long> strideValuesToCount;
    map<long, long> strideDifferencesToCount;
    unsigned long strideZeroCount;
    unsigned long strideZeroDifferenceCount;

    vector< pair<long, long> > topStrideValues;
    vector< pair<long, long> > topStrideDifferenceValues;

    pair<long, long> topStrideHolder;
    
    void updateTopStrideValues(long);
    void updateTopStrideDifferenceValues(long);
};
#endif
