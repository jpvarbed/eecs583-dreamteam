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
#define SSST_T  0.70
#define PMST_T  0.60
#define PMSTD_T 0.40
#define WSST_T  0.25
#define WSSTD_T 0.10
#define MAXPREFETCHDISTANCE 8
#define NUM_TOP_FREQ 4

using namespace llvm;
using namespace std;

struct loadInfo {
    int load_id;            // the id for the load
    int num_strides;        // number of unique stride values
    int exec_count;         // counts the total number of strides analyzed
    int num_zero_diff;      // frequency of a stride of 0
    int dominant_stride;    // value of the most frequent stride value
    int profiled_stride;    // ? TODO
    int trip_count;         // ? TODO
    vector<long> top_freqs; // top X frequencies for the top stride vlaues
};

void profile(Instruction *inst);
void insertLoad(Instruction *inst);
#endif
