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
#include "llvm/BasicBlock.h"
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
#include "StrideLoadProfile.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <map>

#define PRINT_DEBUG 
#ifdef PRINT_DEBUG    
#  define DEBUG(x) x  
#else                 
#  define DEBUG(x)    
#endif                

//#define K_SEARCH
using namespace std;
using namespace llvm;

namespace llvm {
  void initializeStridePrefetchPass(llvm::PassRegistry&);
}

namespace {

  struct StridePrefetch : public LoopPass {
    public:
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
        AU.addRequired<StrideLoadProfile>();
      }

    private:
      AliasAnalysis *AA;
      LoopInfo *LI;
      DominatorTree *DT;
      ProfileInfo *PI;
      StrideLoadProfile *LP;

      typedef map<const Loop * const, unsigned int> InstsPerLoopMap;
      // tracks number of instructions per loop body
      InstsPerLoopMap instsPerLoopMap;

      bool Changed;
      BasicBlock *Preheader;
      Loop *CurrentLoop;

      set <Instruction*> SSST_loads;
      set <Instruction*> PMST_loads;
      set <Instruction*> WSST_loads;

      bool insideSubLoop(BasicBlock *BB) {
        return (LI->getLoopFor(BB) != CurrentLoop);
      }

      loadInfo* getInfo(Instruction* inst);
      void profile(Instruction* inst);
      BinaryOperator *scratchAndSub(Instruction *inst);
      void insertPrefetchInsts(const set<Instruction*>& loads);
      void insertPrefetch(Instruction *inst, const double& K, BinaryOperator *sub, Instruction *before);
      void insertSSST(Instruction *inst, const double& K);
      void insertPMST(Instruction *inst, const double& K);
      void insertWSST(Instruction *inst, const double& K);
      void insertLoad(Instruction *inst);
      void actuallyInsertPrefetch(loadInfo* load_info, Instruction *before, 
          Instruction *address, int locality = 3);
      void loopOver(DomTreeNode *N);
      unsigned getLoopInstructionCount(const Loop * const loop);
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
    Changed = false;

    // clear data structures
    SSST_loads.clear();
    PMST_loads.clear();
    WSST_loads.clear();

    LI = &getAnalysis<LoopInfo>();
    PI = &getAnalysis<ProfileInfo>();
    LP = &getAnalysis<StrideLoadProfile>();
    DT = &getAnalysis<DominatorTree>();

    Preheader = L->getLoopPreheader();
    Preheader->setName(Preheader->getName() + ".preheader");

    CurrentLoop = L;

    loopOver(DT->getNode(L->getHeader()));

    insertPrefetchInsts(SSST_loads); 
    insertPrefetchInsts(PMST_loads); 
    insertPrefetchInsts(WSST_loads); 

    // clear varaibles for the next runOnLoop iteration
    CurrentLoop = 0;
    Preheader = 0;

    return Changed;
  }

// Inserts prefetch instructions for the loads that are SSST, PMST, and WSST.
void StridePrefetch::insertPrefetchInsts(const set<Instruction*>& loads) {
  set<Instruction*>::const_iterator loadIter;
  for (loadIter = loads.begin(); loadIter != loads.end(); ++loadIter) {
    insertLoad(*loadIter);
  }
}

void StridePrefetch::loopOver(DomTreeNode *N) {
  BasicBlock *BB = N->getBlock();

  if (!CurrentLoop->contains(BB)) {
    return; // this subregion is not in the loop so return out of here
  }

  if (!insideSubLoop(BB)) {

    for (BasicBlock::iterator II = BB->begin(), E = BB->end(); II != E; II++) {

      Instruction *I = II;
      if (dyn_cast<LoadInst>(I) && getInfo(I) != NULL) {
        // TODO - decide if this is an instruction to actually profile?
        if(PI->getExecutionCount(I->getParent()) > 0) {
          profile(I);
        }
      }
    }

  }
  const vector<DomTreeNode *> &Children = N->getChildren();
  for (unsigned i = 0, e = Children.size(); i != e; ++i) {
    loopOver(Children[i]);
  }
}

loadInfo* StridePrefetch::getInfo(Instruction* inst) {
  map<Instruction*, loadInfo*>::iterator findInfo;
  findInfo = LP->LoadToLoadInfo.find(inst);
  if (findInfo == LP->LoadToLoadInfo.end()) {
    //errs() << "couldnt find " << *inst << " in getInfo!\n";
    return NULL;
  }
  return findInfo->second;
}

void StridePrefetch::actuallyInsertPrefetch(loadInfo *load_info, 
    Instruction *before, Instruction *address, int locality) {
  errs() << "Prefetching #"<<load_info->load_id<<" with addr: "<<address<<"\n";

  LLVMContext &context = Preheader->getParent()->getContext();
  Module *module = Preheader->getParent()->getParent();
  Constant* prefetchFn;
  prefetchFn = module->getOrInsertFunction(
    "llvm.prefetch",
    llvm::Type::getVoidTy(context),
    llvm::Type::getInt8PtrTy(context),
    llvm::Type::getInt32Ty(context),
    llvm::Type::getInt32Ty(context),
    (Type *) 0
  );

  IntToPtrInst *newAddr = new IntToPtrInst(
    address, llvm::Type::getInt8PtrTy(context), "inttoptr", before);

  vector<Value*> Args(3);
  Args[0] = newAddr; 
  Args[1] = ConstantInt::get(llvm::Type::getInt32Ty(context), 0);
  Args[2] = ConstantInt::get(llvm::Type::getInt32Ty(context), locality);

  // insert the prefetch call
  CallInst::Create(prefetchFn, Args.begin(), Args.end(), "", before);
}

void StridePrefetch::profile(Instruction *inst) {
  int freq1 = 0;
  int exec_count = 0;
  int top_4_freq = 0;
  int zeroDiff = 0;

  loadInfo *profData = getInfo(inst);

  // set the trip_count variable
  profData->trip_count = static_cast<int>(
    PI->getExecutionCount(CurrentLoop->getHeader()) / PI->getExecutionCount(Preheader)
  );

  if (PI->getExecutionCount(inst->getParent()) <= FT) {
    return;
  }
  // assume that loads passed in are in loops
  if (profData->trip_count <= TT) {
    return;
  }

  freq1 = profData->top_freqs[0];
  exec_count = profData->exec_count;
  if(exec_count <= 0){
    errs() << "zerodiff got through\n";
    return;
  }
  assert(exec_count && "exec_count in profile is 0");

  for (unsigned int i = 0; i < profData->top_freqs.size(); i++) {
    top_4_freq += profData->top_freqs[i];
  }

  zeroDiff = profData->num_zero_diff;

  errs() << freq1 << " / "<<exec_count<<" > "<<SSST_T<<"\n";
  // cache line stuff...not sure yet?
  
  errs() << "top4 <" << top_4_freq << "> PMST <" << PMST_T << "> zeroDiff <" << zeroDiff <<"> PMSTD_T <" << PMSTD_T << ">\n";
  if ((double)freq1 / exec_count > SSST_T) {
    SSST_loads.insert(inst);
    errs() << "adding to SSST ("<<profData->dominant_stride<<")\n";
  }
  else if (((double)top_4_freq / exec_count > PMST_T) && ((double)zeroDiff / exec_count) > PMSTD_T) {
    PMST_loads.insert(inst);
    errs() << "adding to PMST\n";
  }
  else if (((double)freq1 / exec_count > WSST_T) && ((double)zeroDiff / exec_count > WSSTD_T)) {
    WSST_loads.insert(inst);
    errs() << "adding to WSST\n";
  }
  else {
    errs() << "adding to none\n";
  }
}

// insert an alloca to hold address
// insert an alloca to hold stride
// insert a subtract stride = addr(load) - scratch
// scratch = addr(load)
BinaryOperator* StridePrefetch::scratchAndSub(Instruction *inst) {

  LLVMContext &context = Preheader->getParent()->getContext();
  // make alloca for scratch reg
  Value *loadAddr = dyn_cast<LoadInst>(inst)->getPointerOperand();
  AllocaInst *scratchPtr = new AllocaInst(
      loadAddr->getType(),
      "scratch" + inst->getName(),
      inst->getParent()->getParent()->getEntryBlock().begin()
      ); 
  LoadInst *loadPtr = new LoadInst(scratchPtr,"loadscr", inst);
  StoreInst *storePtr = new StoreInst(loadAddr, scratchPtr, inst);

  PtrToIntInst *loadInt = new PtrToIntInst(loadAddr, llvm::Type::getInt32Ty(context), "transAddr", inst);
  
  PtrToIntInst *scrInt = new PtrToIntInst(loadPtr, llvm::Type::getInt32Ty(context), "transScr", inst);
  // stride = addr(load) - scratch
  BinaryOperator *subPtr = BinaryOperator::Create(
      Instruction::Sub,
      loadInt,
      scrInt,
      "stride",
      inst
    );
  return subPtr;
}

// inserts prefetch(addr(inst)+sub*K)
void StridePrefetch::insertPrefetch(Instruction *inst, const double& K, BinaryOperator *sub, Instruction *before = NULL) {
  LLVMContext &context = Preheader->getParent()->getContext();

  BinaryOperator *addition;
  Value *loadAddr = dyn_cast<LoadInst>(inst)->getPointerOperand();
  
  if (before == NULL) {
        before = inst;
  }

  PtrToIntInst *newLoadAddr = new PtrToIntInst(loadAddr, llvm::Type::getInt32Ty(context), "ptrtointPre", before);

  if (sub == NULL) {
    // S is the dominant_stride in loadInfo
    int S = getInfo(inst)->dominant_stride;
    int kXs = (int) K * S;

    addition = BinaryOperator::Create(
        Instruction::Add,
        newLoadAddr,
        ConstantInt::get(llvm::Type::getInt32Ty(context), kXs),
        "addition",
        inst
        );
  } else {
    unsigned int newK = (unsigned int)K;
    // round up to the next power of 2
    newK--;
    newK |= newK >> 1;
    newK |= newK >> 2;
    newK |= newK >> 4;
    newK |= newK >> 8;
    newK |= newK >> 16;
    newK++;

    // take the log base 2 of K to get the number of bits to shift left
    newK = (unsigned int) log2(newK);
    
    BinaryOperator *shiftResult = BinaryOperator::Create(
        Instruction::Shl,
        sub,
        ConstantInt::get(llvm::Type::getInt32Ty(context), newK),
        "shiftleft",
        before
        );
    addition = BinaryOperator::Create(
        Instruction::Add,
        newLoadAddr,
        shiftResult,
        "addition",
        before
      );
  }

  actuallyInsertPrefetch(getInfo(inst), before, addition, 0);
}

// insert just prefetch(P+K*S)
void StridePrefetch::insertSSST(Instruction *inst, const double& K) {
  insertPrefetch(inst, K, NULL, inst);
}

// scratch sub
// prefetch(addr(load)+K*stride) before the load, K is rounded to a power of 2, 
void StridePrefetch::insertPMST(Instruction *inst, const double& K) {
  BinaryOperator *subPtr = scratchAndSub(inst);
  insertPrefetch(inst, K, subPtr, inst);
}

// scratch sub
// p=(stride==profiled stride)
// p?prefetch(P+K*stride)
void StridePrefetch::insertWSST(Instruction *inst, const double& K) {

  LLVMContext &context = Preheader->getParent()->getContext();

  BinaryOperator *subPtr = scratchAndSub(inst);

  loadInfo *profData = getInfo(inst);
  int profiled_stride = profData->dominant_stride;

  Value *loadAddr = dyn_cast<LoadInst>(inst)->getPointerOperand();
  PtrToIntInst *AddrToInt = new PtrToIntInst(loadAddr, llvm::Type::getInt32Ty(context), "PtrtoIntWST", inst);

  ICmpInst *ICmpPtr = new ICmpInst(
    inst, 
    ICmpInst::ICMP_EQ,
    AddrToInt,
    ConstantInt::get(llvm::Type::getInt32Ty(context), profiled_stride), 
    "cmpweak"
  );
  
  BasicBlock* homeBB = inst->getParent();
  BasicBlock* prefetchBB = SplitBlock(homeBB, inst, this);
  BasicBlock* restBB = SplitBlock(prefetchBB, inst, this);

  //insert branch
  //
  BranchInst *BI = BranchInst::Create(prefetchBB, restBB, ICmpPtr, homeBB->getTerminator());
  assert(homeBB->getTerminator() != NULL && "homeBB term  is now NULL");
  homeBB->getTerminator()->eraseFromParent(); 

  insertPrefetch(inst, K, subPtr, prefetchBB->getTerminator());
}

// Effects: Recursively calculates the number of instructions executed by loop.
// Modifies: Updates InstsPerLoopMap 
unsigned int StridePrefetch::getLoopInstructionCount(const Loop * const loop) {
  // initialize instsPerLoop entry for loop
  if (instsPerLoopMap.find(loop) == instsPerLoopMap.end()) {
    instsPerLoopMap[loop] = 0;
  }
  else {
    errs() << "loop was already evaluated\n";
    return instsPerLoopMap[loop];
  }

  unsigned int &inst_count = instsPerLoopMap[loop];

  // calculates the average number of instructions executed in the body the loop
  assert(PI->getExecutionCount(loop->getHeader()) > 0 
      && "Execution count shouldn't be negative");
  double loop_exec_count = PI->getExecutionCount(loop->getHeader());
  for (LoopBase<BasicBlock, Loop>::block_iterator biter = loop->block_begin(), end = loop->block_end(); 
      biter != end; ++biter) {
    assert(PI->getExecutionCount(*biter) >= 0 
      && "Execution count shouldn't be negative");

    unsigned int block_exec_count = PI->getExecutionCount(*biter);
    const BasicBlock::InstListType& inst_list = (*biter)->getInstList();
    
    inst_count += block_exec_count * (double)inst_list.size();
  }

  return (double)inst_count / PI->getExecutionCount(loop->getHeader());
}

void StridePrefetch::insertLoad(Instruction *inst) {
  loadInfo *profData = getInfo(inst);
 
  unsigned int total_loop_insts_exec = getLoopInstructionCount(CurrentLoop);
  // number of instructions executed on average in that loop
  unsigned int single_loop_insts_exec = total_loop_insts_exec / PI->getExecutionCount(CurrentLoop->getHeader());
 
  double K = 8;
  errs()<<"totalinstexec: " << total_loop_insts_exec<<"\n";
  if (total_loop_insts_exec < 200) {
    K = MEMORY_LATENCY / total_loop_insts_exec;
  }
  K = min(K, (double)8);
#ifdef K_SEARCH
  K = CHANGE_K_FLAG;
#endif
  double dataArea = profData->dominant_stride * profData->trip_count;

  errs() << "K: " << K << "\n";
  // we can incorporate cache stuff if need be
  // call the corresponding load list
  set <Instruction*>::iterator loadIT;
  if (PMST_loads.count(inst)) {
    insertPMST(inst, K);
  }
  else if (SSST_loads.count(inst)) {
    insertSSST(inst, K);
  }
  else if (WSST_loads.count(inst)) {
    insertWSST(inst, K);
  }
  else {
    errs() << "inst not inserted\n";
  }
}

