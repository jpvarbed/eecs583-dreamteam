/*
  Reads in the stride data
*/

#ifndef STRIDEPLOADPROFILE_H
#define STRIDEPLOADPROFILE_H
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
#include "profilefeedback.h"
#include <map>
#include <sstream>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>

namespace llvm {

  class ModulePass;
  class FunctionPass;

  ModulePass *createStrideLoadProfilePass();

  class StrideLoadProfile : public ModulePass {
  public:
    std::map<unsigned int, Instruction*> IdToInstMap;   // Inst* -> InstId
    std::map<Instruction*, unsigned int> InstToIdMap;   // InstID -> Inst*
    std::map<std::pair<Instruction*, Instruction*>*, unsigned int> DepToTimesMap; 
    std::map<BasicBlock*, std::set<std::pair<Instruction*, Instruction*>* > > LoopToDepSetMap;
    std::map<BasicBlock*, unsigned int> LoopToMaxDepTimesMap;

    std::map<unsigned int, Instruction *> LoadIdToLoadInst;
    std::map<Instruction *, loadInfo *> LoadToLoadInfo;

    static unsigned int stride_id;
    static unsigned int load_id;
    static char ID;
    StrideLoadProfile() : ModulePass (ID) {}

    virtual bool runOnModule (Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  };
}
#endif 
