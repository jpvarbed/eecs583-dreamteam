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
#include <cmath>

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
      void insertPrefetchInsts(const set<Instruction*> loads);
      void insertPrefetch(Instruction *inst, double K, BinaryOperator *sub);
      void insertSSST(Instruction *inst, double K);
      void insertPMST(Instruction *inst, double K);
      void insertWSST(Instruction *inst, double K);
      void insertLoad(Instruction *inst);
      void actuallyInsertPrefetch(loadInfo* load_info, Instruction *before, 
          Instruction *address, int locality = 3);
      void loopOver(DomTreeNode *N);
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

  LI = &getAnalysis<LoopInfo>();
  PI = &getAnalysis<ProfileInfo>();
  LP = &getAnalysis<LAMPLoadProfile>();
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
void StridePrefetch::insertPrefetchInsts(const set<Instruction*> loads) {
  set<Instruction*>::const_iterator loadIter;
  for (loadIter = loads.begin(); loadIter != loads.end(); ++loadIter)
    insertLoad(*loadIter);
}

void StridePrefetch::loopOver(DomTreeNode *N) {
  BasicBlock *BB = N->getBlock();

  if (!CurrentLoop->contains(BB)) {
    return; // this subregion is not in the loop so return out of here
  }

  if (!insideSubLoop(BB)) {

    for (BasicBlock::iterator II = BB->begin(), E = BB->end(); II != E; II++) {
      Instruction *I = II;
      if (dyn_cast<LoadInst>(I)) {
        // TODO - decide if this is an instruction to actually profile?
        profile(I);
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
    errs() << "couldnt find " << *inst << " in getInfo\n";
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
 
  IntToPtrInst *newAddr = new IntToPtrInst(address, llvm::Type::getInt8PtrTy(context), "inttoptr", before);
  //BitCastInst *newAddr = new BitCastInst(address, llvm::Type::getInt8PtrTy(context), "bitcast", before);
  
  vector<Value*> Args(3);
  //Constant *tmp = ConstantInt::get(llvm::Type::getInt8Ty(context), address);
  Args[0] = newAddr; //ConstantExpr::getIntToPtr(tmp, llvm::Type::getInt8PtrTy(context));
  Args[1] = ConstantInt::get(llvm::Type::getInt32Ty(context), 0);
  // Args[2] temporal locality value? ranges from 0 - 3
  Args[2] = ConstantInt::get(llvm::Type::getInt32Ty(context), locality);

  // insert the prefetch call
  CallInst::Create(prefetchFn, Args.begin(), Args.end(), "", before);
}

void StridePrefetch::profile(Instruction *inst) {
  int freq1 = 0;
  int num_strides = 0;
  int top_4_freq = 0;
  int zeroDiff = 0;

  loadInfo *profData = getInfo(inst);
  
  if(PI->getExecutionCount(inst->getParent()) <= FT) {
    return;
  }
  // assume that loads passed in are in loops
  if(PI->getExecutionCount(Preheader) <= TT) {
    return;
  }
  
  freq1 = profData->top_freqs[0];
  num_strides = profData->num_strides;
  
  for(unsigned int i = 0; i < profData->top_freqs.size(); i++) {
    top_4_freq += profData->top_freqs[i];
  }
  
  zeroDiff = profData->num_zero_diff;
  
  // cache line stuff...not sure yet?
  if (freq1/num_strides > SSST_T) {
    SSST_loads.insert(inst);
    errs() << "adding to SSST ("<<profData->dominant_stride<<")\n";
  }
  else if ((top_4_freq/num_strides > PMST_T) && zeroDiff/num_strides > PMST_T) {
    PMST_loads.insert(inst);
    errs() << "adding to PMST\n";
  }
  else if ((freq1/num_strides > WSST_T) && (zeroDiff/num_strides > PMST_T)) {
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
  Module *M = inst->getParent()->getParent()->getParent();
  BasicBlock *entryBlock = &inst->getParent()->getParent()->getEntryBlock();

  // make alloca for scratch reg
  Value *loadAddr = dyn_cast<LoadInst>(inst)->getPointerOperand();
  AllocaInst *scratchPtr = new AllocaInst(
    loadAddr->getType(),
    "scratch" + inst->getName(),
    entryBlock
  ); 

  // TODO this is not a mov!
  // store[scratchPtr] = loadAddr
  StoreInst *storePtr = new StoreInst(scratchPtr, loadAddr, inst);

  // stride = addr(load) - scratch
  BinaryOperator *subPtr = BinaryOperator::Create(
    Instruction::Sub,
    loadAddr,
    scratchPtr,
    "stride",
    storePtr
  );
  return subPtr;
}

// inserts prefetch(addr(inst)+sub*K)
void StridePrefetch::insertPrefetch(Instruction *inst, double K, BinaryOperator *sub) {
  LLVMContext &context = Preheader->getParent()->getContext();

  BinaryOperator *addition;
  Value *loadAddr = dyn_cast<LoadInst>(inst)->getPointerOperand();
  LoadInst *loadAddrLoaded = new LoadInst(loadAddr, "loadaddr", inst);

  if (sub == NULL) {
    // S is the dominant_stride in loadInfo
    int S = getInfo(inst)->dominant_stride;
    int kXs = (int)K * S;

    addition = BinaryOperator::Create(
      Instruction::Add,
      loadAddrLoaded,
      ConstantInt::get(llvm::Type::getInt32Ty(context), kXs),
      "addition",
      inst
    );
  } else {
    // TODO set shiftResult = K*stride... aka shift stride over by K bits (round K to power of two)

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
    newK = log2(newK);
    
    BinaryOperator *shiftResult = BinaryOperator::Create(
      Instruction::Shl,
      sub,
      ConstantInt::get(llvm::Type::getInt32Ty(context), newK),
      "shiftleft",
      inst
    );

    addition = BinaryOperator::Create(
      Instruction::Add,
      loadAddrLoaded,
      shiftResult,
      "addition",
      inst
    );
  }
  
  actuallyInsertPrefetch(getInfo(inst), inst, addition, 0);
}

// insert just prefetch(P+K*S)
void StridePrefetch::insertSSST(Instruction *inst, double K) {
  insertPrefetch(inst, K, NULL);
}

// scratch sub
// prefetch(addr(load)+K*stride) before the load, K is rounded to a power of 2, 
void StridePrefetch::insertPMST(Instruction *inst, double K) {
  BinaryOperator *subPtr = scratchAndSub(inst);
  insertPrefetch(inst, K, subPtr);
}

// scratch sub
// p=(stride==profiled stride)
// p?prefetch(P+K*stride)
void StridePrefetch::insertWSST(Instruction *inst, double K) {
  BinaryOperator *subPtr = scratchAndSub(inst);

  loadInfo *profData = getInfo(inst);
  int profiled_stride = profData->profiled_stride;
  
  ICmpInst *ICmpPtr = new ICmpInst(
    inst, 
    ICmpInst::ICMP_EQ,
    subPtr,
    ConstantInt::get(dyn_cast<LoadInst>(inst)->getPointerOperand()->getType(), profiled_stride), 
    "cmpweak"
  );

  insertPrefetch(inst, K, subPtr);
}

void StridePrefetch::insertLoad(Instruction *inst) {
  loadInfo *profData = getInfo(inst);
  
  // TODO: determine K
  double K = 0;
  double dataArea = profData->dominant_stride * profData->trip_count;
  double C = MAXPREFETCHDISTANCE;
  K = min((double) profData->trip_count/TT, C);

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

