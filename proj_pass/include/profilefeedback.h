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
#define FT 10000
#define TT 100
#define SSST_T 10
#define PMST_T 10
#define WSST_T 10
#define MAXPREFETCHDISTANCE 8

using namespace llvm;
using namespace std;

struct loadInfo {
    int load_id;
    vector<long> top_freqs;
    int num_zero_diff;
    int profiled_stride;
    int num_strides;
    int dominant_stride;
    int trip_count;
};

void profile(Instruction *inst);
void insertLoad(Instruction *inst);
#endif
