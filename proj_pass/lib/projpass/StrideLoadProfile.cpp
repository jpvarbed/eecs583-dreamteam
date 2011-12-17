/*
  Read in the stride data
*/

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
#include <algorithm>
#include <sys/stat.h>
#include "StrideLoadProfile.h"

using namespace llvm;

inline unsigned int str_to_int(std::string& s)
{
  std::istringstream iss(s);
  unsigned int t;
  iss >> t;
  return t;	
}

namespace {
  class LdStCallCounter : public ModulePass {
    public:
      static char ID;
      static bool flag;
      bool runOnModule(Module &M);
      static unsigned int num_loads;

      //    static unsigned int num_loops;
      LdStCallCounter(): ModulePass(ID) {}
      
      unsigned int getCountInsts() { return num_loads; }
  };
}

char LdStCallCounter::ID = 0;

// flag to ensure we only count once
bool LdStCallCounter::flag = false;

// only want these counted once and only the first time (not after other instrumentation)
unsigned int LdStCallCounter::num_loads = 0;	

static RegisterPass<LdStCallCounter>
A("stride-inst-cnt",
    "Count the number of Stride Profilable insts");

  bool LdStCallCounter::runOnModule(Module &M) {
    if (flag == true)	{ // if we have counted already -- structure of llvm means this could be called many times
      return false;
    }
    // for all functions in module
    for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
      if (!I->isDeclaration()) {			// for all blocks in the function
        for (Function::iterator BBB = I->begin(), BBE = I->end(); BBB != BBE; ++BBB) {		// for all instructions in a block
          for (BasicBlock::iterator IB = BBB->begin(), IE = BBB->end(); IB != IE; IB++) {
            if (isa<LoadInst>(IB)) {		// count loads, stores, calls
              num_loads++;
            }
          }
        }
      }
    flag = true;

    return false;
  }

void StrideLoadProfile::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}

char StrideLoadProfile::ID = 0;
unsigned int StrideLoadProfile::stride_id = -1;
unsigned int StrideLoadProfile::load_id = -1;
static RegisterPass<StrideLoadProfile> Z("stride-load-profile","Load back profile data and generate dependency information");

bool StrideLoadProfile::runOnModule(Module& M) {
  // build the <IDs, Instrucion> map
  for (Module::iterator FB = M.begin(), FE = M.end(); FB != FE; FB++) {
    if (!FB->isDeclaration()) {
      for (Function::iterator BBB = FB->begin(), BBE = FB->end(); BBB != BBE; ++BBB) {
        for (BasicBlock::iterator IB = BBB->begin(), IE = BBB->end(); IB != IE; IB++) {
          if (isa<LoadInst>(IB)) {
            LoadIdToLoadInst[++load_id] = IB; // TODO remove and replace with normal stride_id?
            llvm::errs() << load_id << " : " << IB << "\n";
          }

          if (isa<LoadInst>(IB) || isa<StoreInst>(IB)){ // count loads, stores, calls
            IdToInstMap[++stride_id] = IB;
            InstToIdMap[IB] = stride_id;
          }
          else if (isa<CallInst>(IB) && ( (dyn_cast<CallInst>(IB)->getCalledFunction() == NULL) || 
                (dyn_cast<CallInst>(IB)->getCalledFunction()->isDeclaration()))) {
            IdToInstMap[++stride_id]=IB;
            InstToIdMap[IB]=stride_id;
          }
        }
      }
    }
  }
  std::ifstream ifs;
  struct stat sInfo;

  if(stat("result.stride.profile", &sInfo ) != 0) {
    std::cerr << "Could not find file result.stride.profile\n";
    return false;
  }
  
  try{
    ifs.open("result.stride.profile");
  } catch (...){
    std::cerr << "Could not find file result.stride.profile\n";
    return false;
  }
  
  std::string s;

  getline(ifs, s); // get rid of the the STRIDEPROFILE_START
  while (getline(ifs, s)) {
    if (s.find("STRIDEPROFILE_END") != std::string::npos) {
      break;
    }

    loadInfo *load_info = new loadInfo;
    unsigned int freq[4];
    sscanf(
        s.c_str(),
        "%d %d %d %d %d %u %u %u %u",
        &(load_info->load_id),
        &(load_info->num_strides),
        &(load_info->exec_count),
        &(load_info->num_zero_diff),
        &(load_info->dominant_stride),
        &(freq[0]), &(freq[1]), &(freq[2]), &(freq[3])
        );

    llvm::errs() << "Stride Profile ("<<LoadIdToLoadInst[load_info->load_id]<<"): "<<s<<"\n";
    Instruction *loadinstr = LoadIdToLoadInst[load_info->load_id];

    for (int a = 0; a < NUM_TOP_FREQ; a++) {
      load_info->top_freqs.push_back(freq[a]);
    }
    sort(load_info->top_freqs.rbegin(), load_info->top_freqs.rend()); // sort desc order

    LoadToLoadInfo[loadinstr] = load_info;
  }

  return true;
}
