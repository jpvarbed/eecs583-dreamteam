#ifndef PROFILEFEEDBACK_H_
#define PROFILEFEEDBACK_H_

#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Function.h"
#include "llvm/BasicBlock.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetData.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/Module.h"
#include <map>
#include <sstream>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <set>
#define FT 2000
#define TT 100
#define SSST_T 10
#define PMST_T 10
#define WSST_T 10
#define MAXPREFETCHDISTANCE 8

using namespace llvm;
using namespace std;

struct loadInfo {
    int load_id;            // the id for the load
    int num_strides;        // number of unique stride values
    int num_zero_diff;      // frequency of a stride of 0
    int dominant_stride;    // value of the most frequent stride value
    vector<long> top_freqs; // top X frequencies for the top stride vlaues
    int profiled_stride;    // ? TODO (all)
    int trip_count;
};

void profile(Instruction *inst);
void insertLoad(Instruction *inst);
#endif
