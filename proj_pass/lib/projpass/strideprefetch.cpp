/*
   Stride Profiling & Prefetching

   EECS 583 - Project
   Jason Varbedian, Patryk Mastela, Matt Viscomi

   Based off of:
   Efficient Discovery of Regular Stride Patterns in Irregular
   Programs and Its Use in Compiler Prefetching
   by Youfeng Wu
 */

#include "llvm/Transforms/Scalar.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/LLVMContext.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/ProfileInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/ADT/Statistic.h"
#include "profilefeedback.h"
#include "LAMPLoadProfile.h"
#include <vector>
#include <algorithm>
using namespace llvm;

namespace llvm {
  void initializeStridePrefetchPass(llvm::PassRegistry&);
}

namespace {
  struct StridePrefetch : public LoopPass {

    static char ID;

    StridePrefetch() : LoopPass(ID) {
      initializeStridePrefetchPass(*PassRegistry::getPassRegistry());
    }

    virtual bool runOnLoop(Loop *L, LPPassManager &LPM);

    /// This transformation requires natural loop information & requires that
    /// loop preheaders be inserted into the CFG...
    ///
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<DominatorTree>();
      AU.addRequired<LoopInfo>();
      AU.addRequired<ProfileInfo>();
      AU.addRequired<AliasAnalysis>();
      AU.addRequired<LAMPLoadProfile>();
    }
    private:
    AliasAnalysis *AA;
    LoopInfo *LI;
    DominatorTree *DT;
    ProfileInfo *PI;
    LAMPLoadProfile *LP;
    
    set <Instruction*> SSST_loads;
    set <Instruction*> PMST_loads;
    set <Instruction*> WSST_loads;

    loadInfo* getInfo(Instruction* inst);
    void profile(Instruction* inst);
    BinaryOperator *scratchAndSub(Instruction *inst);
    void insertPrefetch(Instruction *inst, double K, BinaryOperator *sub);
    void insertSSST(Instruction *inst, double K);
    void insertPMST(Instruction *inst, double K);
    void insertWSST(Instruction *inst, double K);
    void insertLoad(Instruction *inst);
    void actuallyInsertPrefetch(Instruction *before, long address, int locality); 
  };
}

namespace llvm {
  Pass *createStridePrefetch() {
    return new StridePrefetch();
  }
}

char StridePrefetch::ID = 0;
  INITIALIZE_PASS_BEGIN(StridePrefetch, "strideprefetch", "Stride Prefetching", false, false)
  INITIALIZE_PASS_DEPENDENCY(DominatorTree)
  INITIALIZE_PASS_DEPENDENCY(LoopInfo)
  INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
  INITIALIZE_AG_DEPENDENCY(AliasAnalysis)
  INITIALIZE_PASS_END(StridePrefetch, "strideprefetch", "Stride Prefetching", false, false)
  static RegisterPass<StridePrefetch> X("projpass", "LICM Pass", true, true);

bool StridePrefetch::runOnLoop(Loop *L, LPPassManager &LPM) {
  bool Changed = false;

  LP = &getAnalysis<LAMPLoadProfile>();

  return Changed;
}

loadInfo* StridePrefetch::getInfo(Instruction* inst) {
  map<Instruction*, loadInfo *>::iterator findInfo;
  findInfo = LP->LoadToLoadInfo.find(inst);
  if(findInfo == LP->LoadToLoadInfo.end()){
    errs() << "couldnt find " << *inst << " in getInfo\n";
  }
  return findInfo->second;
}

void StridePrefetch::actuallyInsertPrefetch(Instruction *before, long address, int locality) {
  // TODO: need to define M (Module)
  Constant* prefetchFn;
  prefetchFn = M->getOrInsertFunction(
    "int_prefetch",
    llvm::Type::getVoidTy(M->getContext()),
    llvm::Type::getInt8Ty(M->getContext()),
    llvm::Type::getInt32Ty(M->getContext()),
    llvm::Type::getInt32Ty(M->getContext()),
    llvm::Type::getInt32Ty(M->getContext()),
    (Type *) 0
  );

  vector<Value*> Args(4);
  // Args[0] needs to be the address to prefetch... but why is it an Int8Ty?
  Args[1] = ConstantInt::get(llvm::Type::getInt32Ty(M->getContext()), 0); // 0 is read
  // Args[2] temporal locality value? ranges from 0 - 3
  Args[3] = ConstantInt::get(llvm::Type::getInt32Ty(M->getContext()), 1); // 1 data prefetch

  // insert the prefetch call
  CallInst::Create(prefetchFn, Args.begin(), Args.end(), "", before);
}

// TODO shouldn't this be in a loop over the load insts?
void StridePrefetch::profile(Instruction *inst) {
  int freq1 = 0;
  int num_strides = 0;
  int top_4_freq = 0;
  int zeroDiff = 0;

  loadInfo profData = *getInfo(inst);

  if(PI->getExecutionCount(inst->getParent()) <= FT)
    return;
  //assume that loads passed in are in loops
  if(PI->getExecutionCount(Preheader) <= TT)
    return;
  freq1 = profData.top_freqs[0];
    num_strides = profData.num_strides;
  for(unsigned int i = 0; i < profData.top_freqs.size(); i++)
    top_4_freq+=profData.top_freqs[i];
  zeroDiff = profData.num_zero_diff;
  //cache line stuff...not sure yet?
  if(freq1/num_strides > SSST_T){
    SSST_loads.insert(inst);
    printf("adding to SSST\n");
  }
  else if((top_4_freq/num_strides > PMST_T) && zeroDiff/num_strides > PMST_T){
    PMST_loads.insert(inst);
    printf("adding to PMST\n");
  }
  else if((freq1/num_strides > WSST_T) && (zeroDiff/num_strides > PMST_T))
  {
    WSST_loads.insert(inst);
    printf("adding to WSST\n");
  }
  else{
    printf("adding to none\n");
  }
}
//insert an alloca to hold address
//insert an alloca to hold stride
//insert a subtract stride = addr(load) - scratch
//scratch = addr(load)
BinaryOperator* StridePrefetch::scratchAndSub(Instruction *inst) {
  AllocaInst *scratchPtr;
  StoreInst *storePtr;
  BinaryOperator *subPtr;
  //inst->bb->fcn->module
  Module *M = inst->getParent()->getParent()->getParent();
  BasicBlock *entryBlock = inst->getParent()->getParent()->getEntryBlock().begin();

  StringRef X = "scratch";
  StringRef Name = inst->getName();
  //make alloca for scratch reg
  Value *loadAddr = inst->getPointerOperand();
  scratchPtr = new AllocaInst(loadAddr->getType(), X + Name, entryBlock); 

  //store[scratchPtr] = loadAddr
  storePtr = new StoreInst(scratchPtr, loadAddr, inst);

  //stride = addr(load) - scratch
  subPtr = BinaryOperator::Create(Instruction::Sub,
    loadAddr,
    scratchPtr,
    "stride",
    inst
  );
  return subPtr;
}

//inserts prefetch(addr(inst)+sub*K)
void StridePrefetch::insertPrefetch(Instruction *inst, double K, BinaryOperator *sub) {
}

//insert just prefetch(P+K*S)
void StridePrefetch::insertSSST(Instruction *inst, double K) {
  insertPrefetch(inst, K, NULL);
}

//scratch sub
//prefetch(addr(load)+K*stride) before the load, K is rounded to a power of 2, 
void StridePrefetch::insertPMST(Instruction *inst, double K) {
  BinaryOperator *subPtr = scratchAndSub(inst);
  insertPrefetch(inst, K, subPtr);
}

//scratch sub
//p=(stride==profiled stride)
//p?prefetch(P+K*stride)
void StridePrefetch::insertWSST(Instruction *inst, double K) {
  BinaryOperator *subPtr = scratchAndSub(inst);
  ICmpInst *ICmpPtr;
  loadInfo profData = *getInfo(inst);
  int profiled_stride = profData.profiled_stride;
  ICmpPtr = new ICmpInst(inst, 
      ICmpInst::ICMP_EQ,
      subPtr,
      ConstantInt::get(inst->getPointerOperand->getType(), profiled_stride), 
      "cmpweak");
  insertPrefetch(inst, K, subPtr);
}

void StridePrefetch::insertLoad(Instruction *inst) {
  //determine K
  double K = 0;
  loadInfo profData = *getInfo(inst);
  double dataArea = profData.dominant_stride * profData.trip_count;
  double C = MAXPREFETCHDISTANCE;
  K = min((double) profData.trip_count/TT, C);
  //we can incorporate cache stuff if need be
  //call the corresponding load list
  set <Instruction*>::iterator loadIT;
  if(PMST_loads.count(inst))
    insertPMST(inst,K);
  else if(SSST_loads.count(inst))
    insertSSST(inst,K);
  else if(WSST_loads.count(inst))
    insertWSST(inst, K);
  else
    errs() << "inst not inserted\n";
}
