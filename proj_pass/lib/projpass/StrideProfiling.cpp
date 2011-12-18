/*
  Instrument the program with stride profiling
*/

#include "llvm/Analysis/ProfileInfo.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Support/Compiler.h"
#include "StrideProfiling.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"	
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/Passes.h"

#include "llvm/Support/Debug.h"
#include <iostream>
#include <set>
#include <map>
#include <algorithm>

#include "loadstride.h"
#include "profilefeedback.h"

using namespace llvm;
using namespace std;

namespace {
  class StrideProfiler : public FunctionPass {
    bool runOnFunction(Function& F);
    void doStrides();
    bool isLoadDynamic(Instruction *inst);
    ProfileInfo *PI;
    LoopInfo *LI;
    Constant* StrideProfileFn;
    Constant* StrideProfileClearAddresses;
    void createLampDeclarations(Module* M);
    TargetData* TD;
    bool ranOnce;
    public:
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<ProfileInfo>();
      AU.addRequired<TargetData>();
      AU.addRequired<LoopInfo>();
    }

    bool doInitialization(Module &M) { return false; }
    static unsigned int instruction_id;

    static unsigned int load_id; // counts the loads that we are analyzing bro

    static char ID;
    StrideProfiler() : FunctionPass(ID) {
      //instruction_id = 0;
      ranOnce = false;
      TD = NULL;
      LI = NULL;
      PI = NULL;
    } 
  };
}

char StrideProfiler::ID = 0;
unsigned int StrideProfiler::instruction_id = -1;
unsigned int StrideProfiler::load_id = -1;

static RegisterPass<StrideProfiler>
X("insert-stride-profiling",
    "Insert instrumentation for Stride profiling");

FunctionPass *llvm::createStrideProfilerPass() { return new StrideProfiler(); }

void StrideProfiler::createLampDeclarations(Module* M)
{
  StrideProfileFn = M->getOrInsertFunction(
    "Stride_StrideProfile",
    llvm::Type::getVoidTy(M->getContext()),
    llvm::Type::getInt32Ty(M->getContext()),
    llvm::Type::getInt64Ty(M->getContext()),
    llvm::Type::getInt32Ty(M->getContext()),
    (Type *) 0
  );

  StrideProfileClearAddresses = M->getOrInsertFunction(
    "Stride_StrideProfile_ClearAddresses",
    llvm::Type::getVoidTy(M->getContext()),
    llvm::Type::getInt32Ty(M->getContext()),
    (Type *) 0
  );
}

vector<Instruction *> loadsToStride;
map<Instruction *, double> loadToExecCount;

bool StrideProfiler::isLoadDynamic(Instruction *inst)
{
    Loop *CurLoop = NULL;
    LoopInfo &LI = getAnalysis<LoopInfo>();
    
    for(LoopInfo::iterator IT = LI.begin(), ITe = LI.end(); IT != ITe; ++IT)
    {
        if ((*IT)->contains(inst)) {
            CurLoop = *IT;
            break;
        }
    }
    
    if (CurLoop == NULL) {
        return false;
    }

    //If operands are loop invariant, you are always loading the same address
    //strides of 0 get no advantage of prefetch so don't waste time profiling
    return !CurLoop->hasLoopInvariantOperands(inst);
}

void StrideProfiler::doStrides() {
  Instruction *I;
  Value *compare;
  BinaryOperator *newNum;
  int num_profiled = 0;
  for (unsigned int i = 0; i < loadsToStride.size(); i++) {
    I = loadsToStride[i];

    load_id++;
    
    if(!isLoadDynamic(I) || loadToExecCount[I] < FT) {
        continue;
    }
    
    num_profiled++;
    errs() << "Found dynamic load id<" << load_id << "> ("<<(loadToExecCount[I])<<") inst " << *I <<"\n";

    int chunkSize = 30;
    double exec_count = loadToExecCount[I];
    // N2 = number to profile ; N1 = number to skip
    int tmpN2 = (int) (1.0/20.0 * exec_count);
    tmpN2 = tmpN2 < 1 ? (int)exec_count : tmpN2;
    tmpN2 = min(tmpN2, chunkSize);
    int tmpN1 = (int) (3.0 / 20.0 * exec_count);
    tmpN1 = tmpN2 == exec_count ? 0 : tmpN1;
    tmpN1 = tmpN2 == chunkSize ?
      tmpN1 + (int)(1.0/20.0 * exec_count) - chunkSize : tmpN1;

    errs() << "Profile Num: " << tmpN2 << "\n";
    errs() << "Skip Num: " << tmpN1 << "\n";

    Value *number_skipped = new GlobalVariable(
      *(I->getParent()->getParent()->getParent()),
      Type::getInt32Ty(I->getParent()->getContext()),
      false,
      llvm::GlobalValue::LinkerPrivateLinkage,
      ConstantInt::get(Type::getInt32Ty(I->getParent()->getContext()), 0),
      "number_skipped"
    );
    
    Value *number_profiled = new GlobalVariable(
      *(I->getParent()->getParent()->getParent()),
      Type::getInt32Ty(I->getParent()->getContext()),
      false,
      llvm::GlobalValue::LinkerPrivateLinkage,
      ConstantInt::get(Type::getInt32Ty(I->getParent()->getContext()), 0),
      "number_profiled"
    );

    BasicBlock *oldBB, *loadBB, *skippingBB,
               *profCheck, *resetBB, *strideBB;
    oldBB = I->getParent();
    loadBB = SplitBlock(oldBB, I, this);
    skippingBB = SplitBlock(oldBB, oldBB->getTerminator(), this);

    profCheck = BasicBlock::Create(I->getParent()->getParent()->getContext(), "profCheck", I->getParent()->getParent(), oldBB);
    resetBB = BasicBlock::Create(I->getParent()->getParent()->getContext(), "resetBB", I->getParent()->getParent(), oldBB); 
    strideBB = BasicBlock::Create(I->getParent()->getParent()->getContext(), "strideBB", I->getParent()->getParent(), oldBB);
    BranchInst::Create(resetBB, profCheck); 
    BranchInst::Create(loadBB, resetBB);
    BranchInst::Create(loadBB, strideBB);
    
    // insert conditional from profCheck to resetBB (true) or strideBB (false)
    LoadInst *number_profiled_load = new LoadInst(number_profiled, "numload", profCheck->getTerminator());
    compare = new ICmpInst(
      profCheck->getTerminator(),
      ICmpInst::ICMP_EQ,
      number_profiled_load,
      ConstantInt::get(Type::getInt32Ty(I->getParent()->getContext()), tmpN2),
      "profcheckCompare"
    );
    BranchInst::Create(resetBB, strideBB, compare, profCheck->getTerminator());
    profCheck->getTerminator()->eraseFromParent();

    // insert conditional from oldBB to skippingBB (true) or profCheck (false)
    LoadInst *number_skipped_load = new LoadInst(number_skipped, "skipload", oldBB->getTerminator());
    compare = new ICmpInst(
      oldBB->getTerminator(),
      ICmpInst::ICMP_ULT, //ult = unsigned less than
      number_skipped_load,
      ConstantInt::get(Type::getInt32Ty(I->getParent()->getContext()), tmpN1),
      "oldbbCompare"
    );
    BranchInst::Create(skippingBB, profCheck, compare, oldBB->getTerminator());
    oldBB->getTerminator()->eraseFromParent();

    // number_skipped++ in the skippingBB
    newNum = BinaryOperator::Create(
      Instruction::Add,
      number_skipped_load,
      ConstantInt::get(Type::getInt32Ty(I->getParent()->getContext()), 1),
      "number_skip_inc",
      skippingBB->getTerminator()
    );
    new StoreInst(
      newNum,
      number_skipped,
      skippingBB->getTerminator()
    );
    
    // set number_profiled and number_skipped to 0 in resetBB
    // also clear addresses in class
    new StoreInst(
      ConstantInt::get(Type::getInt32Ty(resetBB->getContext()), 0),
      number_profiled,
      resetBB->getTerminator()
    );
    new StoreInst(
      ConstantInt::get(Type::getInt32Ty(resetBB->getContext()), 0),
      number_skipped,
      resetBB->getTerminator()
    );
    std::vector<Value*> StrideClearArgs(1);
    StrideClearArgs[0] = ConstantInt::get(
      llvm::Type::getInt32Ty(I->getParent()->getParent()->getContext()),
      load_id
    );
    CallInst::Create(
      StrideProfileClearAddresses,
      StrideClearArgs.begin(),
      StrideClearArgs.end(),
      "",
      resetBB->getTerminator()
    );

    newNum = BinaryOperator::Create(
      Instruction::Add,
      number_skipped_load,
      ConstantInt::get(Type::getInt32Ty(I->getParent()->getContext()), 1),
      "number_skip_inc",
      strideBB->getTerminator()
    );
    new StoreInst(
      newNum,
      number_skipped,
      strideBB->getTerminator()
    );

    std::vector<Value*> StrideArgs(3);
    StrideArgs[0] = ConstantInt::get(
      llvm::Type::getInt32Ty(I->getParent()->getParent()->getContext()),
      load_id
    );
    StrideArgs[1] = new PtrToIntInst(
      (dyn_cast<LoadInst>(I))->getPointerOperand(),
      llvm::Type::getInt64Ty(I->getParent()->getParent()->getContext()),
      "addr_var",
      strideBB->getTerminator()
    );
    StrideArgs[2] = ConstantInt::get(
      llvm::Type::getInt32Ty(I->getParent()->getParent()->getContext()),
      (int)exec_count
    );

    CallInst::Create(
      StrideProfileFn,
      StrideArgs.begin(),
      StrideArgs.end(),
      "",
      strideBB->getTerminator()
    ); 
  }
  errs() << "num_profiled is <"<<num_profiled<<"> \n";
}

bool StrideProfiler::runOnFunction(Function &F) {
  if (ranOnce == false) {
    Module* M = F.getParent();
    createLampDeclarations(M);
    ranOnce = true;
  }

  if (TD == NULL) {
    TD = &getAnalysis<TargetData>();
  }
  if (LI == NULL) {
    LI = &getAnalysis<LoopInfo>();
  }
  if (PI == NULL) {
    PI = &getAnalysis<ProfileInfo>();
  }
  
  //DOUT << "Instrumenting Function " << F.getName() << " beginning ID: " << instruction_id << std::endl;

  for (Function::iterator IF = F.begin(), IE = F.end(); IF != IE; ++IF) {
    BasicBlock& BB = *IF;
    
    for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; ++I) {
      if (isa<LoadInst>(I)) {
        instruction_id++;
        errs() << instruction_id <<" is: "<< *I << "\n";
        
        // TODO only call this function if this load has some freq count above some threshold (use edge profiling to figure this out)
        loadToExecCount[I] = PI->getExecutionCount(I->getParent());
        loadsToStride.push_back(I);
      }
    }
  }

  doStrides();

  return true;
}




namespace {
  class StrideInit : public ModulePass {
    bool runOnModule(Module& M);

    public:
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    }

    static char ID;
    StrideInit() : ModulePass(ID) { }
  };
}

char StrideInit::ID = 0;

static RegisterPass<StrideInit>
V("insert-stride-init", "Insert initialization for Stride profiling");

ModulePass *llvm::createStrideInitPass() { return new StrideInit(); }

bool StrideInit::runOnModule(Module& M) {
  for(Module::iterator IF = M.begin(), E = M.end(); IF != E; ++IF) {
    Function& F = *IF;
    if (F.getName() == "main") {
      const char* FnName = "Stride_init";

      Constant *InitFn = M.getOrInsertFunction(FnName, llvm::Type::getVoidTy(M.getContext()), (Type*) 0);
      BasicBlock& entry = F.getEntryBlock();
      BasicBlock::iterator InsertPos = entry.begin();
      while (isa<AllocaInst>(InsertPos)) {
        ++InsertPos;
      }

      CallInst::Create(InitFn, "", InsertPos);														
      return true;
    }
  }
  return false;
}

