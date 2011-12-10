#ifndef LOADSTRIDE_H
#define LOADSTRIDE_H
#include <vector>
#include <utility>
#include <map>
#include <stdint.h>

using namespace std;
class LoadStride {

  public:
    LoadStride(uint32_t);
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

  private:
    static unsigned int TOPCOUNT;

    uint32_t loadID; 

    vector<unsigned long> addresses;
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
