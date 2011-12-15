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
#include "LAMPProfiling.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"	//TRM 7/21/08
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/Passes.h"	//TRM 7/21/08

#include "llvm/Support/Debug.h"
#include <iostream>
#include <set>
#include <map>
#include <algorithm>
#include "loadstride.h"

using namespace llvm;
using namespace std;

// This class is a module pass designed to do no modification or instrumentation but count the number of
// loads, stores, and calls for the initialization call.  It also tracks the loop counts generated by the
// loop profiler so they can be accessed by the initializing pass.
namespace {
  class LdStCallCounter : public ModulePass {
    public:
      static char ID;
      static bool flag;
      bool runOnModule(Module &M);
      static unsigned int num_loads;
      static unsigned int num_stores;
      static unsigned int num_calls;
      static unsigned int num_loops;
      LdStCallCounter(): ModulePass(ID)
    {

    }	
      unsigned int getCountInsts() { return num_loads + num_stores + num_calls; }
  };

}

char LdStCallCounter::ID = 0;

// flag to ensure we only count once
bool LdStCallCounter::flag = false;

// only want these counted once and only the first time (not after other instrumentation)
unsigned int LdStCallCounter::num_loads = 0;	
// store loops here also because loop passes cannot be required by other passes
unsigned int LdStCallCounter::num_loops = 0;	

static RegisterPass<LdStCallCounter>
Y("lamp-insts",
    "Count the number of LAMP Profilable insts");

ModulePass *llvm::createLdStCallCounter() {
  return new LdStCallCounter();
}

  bool LdStCallCounter::runOnModule(Module &M) {
    if (flag == true)	// if we have counted already -- structure of llvm means this could be called many times
      return false;
    // for all functions in module
    for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
      if (!I->isDeclaration())
      {			// for all blocks in the function
        for (Function::iterator BBB = I->begin(), BBE = I->end(); BBB != BBE; ++BBB)
        {		// for all instructions in a block
          for (BasicBlock::iterator IB = BBB->begin(), IE = BBB->end(); IB != IE; IB++)
          {
            if (isa<LoadInst>(IB)) {		// count loads, stores, calls
              num_loads++;
            }
          }
        }
      }
    //DOUT << "Loads/Store/Calls:" << num_loads << " " << num_stores << " " << num_calls << std::endl;
    flag = true;

    return false;
  }

// LAMPProfiler instruments loads, stores, and calls.  Target data required to determine
// data size to be profiled.
namespace {
  class LAMPProfiler : public FunctionPass {
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
    LAMPProfiler() : FunctionPass(ID) 
    {
      //instruction_id = 0;
      ranOnce = false;
      TD = NULL;
      LI = NULL;
      PI = NULL;
    } 
  };
}

char LAMPProfiler::ID = 0;
unsigned int LAMPProfiler::instruction_id = -1;
unsigned int LAMPProfiler::load_id = -1;

static RegisterPass<LAMPProfiler>
X("insert-lamp-profiling",
    "Insert instrumentation for LAMP profiling");

FunctionPass *llvm::createLAMPProfilerPass() { return new LAMPProfiler(); }

void LAMPProfiler::createLampDeclarations(Module* M)
{
  StrideProfileFn = M->getOrInsertFunction(
    "LAMP_StrideProfile",
    llvm::Type::getVoidTy(M->getContext()),
    llvm::Type::getInt32Ty(M->getContext()),
    llvm::Type::getInt64Ty(M->getContext()),
    llvm::Type::getInt32Ty(M->getContext()),
    (Type *) 0
  );

  StrideProfileClearAddresses = M->getOrInsertFunction(
    "LAMP_StrideProfile_ClearAddresses",
    llvm::Type::getVoidTy(M->getContext()),
    llvm::Type::getInt32Ty(M->getContext()),
    (Type *) 0
  );
}

vector<Instruction *> loadsToStride;
map<Instruction *, unsigned int> loadToExecCount;

bool LAMPProfiler::isLoadDynamic(Instruction *inst)
{
    Loop *CurLoop = NULL;
    LoopInfo &LI = getAnalysis<LoopInfo>();
    bool result = true;
    for(LoopInfo::iterator IT = LI.begin(), ITe = LI.end(); IT != ITe; ++IT)
    {
        if((*IT)->contains(inst)){
            CurLoop = *IT;
            break;
        }
    }
    if(CurLoop == NULL){
        errs() << "Couldn't find Loop inst belongs to. Uh oh\n";
        return false;
    }
    //If operands are loop invariant, you are always loading the same address
    //strides of 0 get no advantage of prefetch so don't waste time profiling
    
    errs() << "Found loop\n";
    result = !CurLoop->hasLoopInvariantOperands(inst);
    return result;
}
void LAMPProfiler::doStrides() {
  Instruction *I;
  Value *compare;
  BinaryOperator *newNum;

  for (int i = 0; i < loadsToStride.size(); i++) {
    I = loadsToStride[i];

    load_id++;
    
    if(!isLoadDynamic(I))
        continue;
    errs() << "Found dynamic load\n";
    int chunkSize = 30;
    int exec_count = loadToExecCount[I];
    // N2 = number to profile ; N1 = number to skip
    int tmpN2 = 1.0/20.0 * (double)exec_count;
    tmpN2 = tmpN2 < 1 ? exec_count : tmpN2;
    tmpN2 = min(tmpN2, chunkSize);
    int tmpN1 = 3.0 / 20.0 * (double)exec_count;
    tmpN1 = tmpN2 == exec_count ? 0 : tmpN1;
    tmpN1 = tmpN2 == chunkSize ? tmpN1 + 1.0/20.0 * exec_count - chunkSize : tmpN1;

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
      exec_count
    );

    CallInst::Create(
      StrideProfileFn,
      StrideArgs.begin(),
      StrideArgs.end(),
      "",
      strideBB->getTerminator()
    ); 
  }
}

bool LAMPProfiler::runOnFunction(Function &F) {
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





// This class retrieves data from the LdStCallCounter class.  While not explicitly noted for llvm structural
// reasons, this class does require that insert-lamp-loop-profiling (LAMPLoopProfiler class) run first.  If it
// fails to run first, the number of loops will be reported as zero.  Initialization pass should be run LAST.
namespace {
  class LAMPInit : public ModulePass {
    bool runOnModule(Module& M);

    public:
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<LdStCallCounter>();
      //  AU.addRequired<LAMPLoopProfiler>();  LAMPLoopProfiler MUST run first but we cannot add required due to 
    }						// LLVM structural issues

    static char ID;
    LAMPInit() : ModulePass(ID) 
    { } 
  };
}

char LAMPInit::ID = 0;

static RegisterPass<LAMPInit>
V("insert-lamp-init",
    "Insert initialization for LAMP profiling");

ModulePass *llvm::createLAMPInitPass() { return new LAMPInit(); }

bool LAMPInit::runOnModule(Module& M)
{
  for(Module::iterator IF = M.begin(), E = M.end(); IF != E; ++IF)
  {
    Function& F = *IF;
    if (F.getName() == "main") {
      const char* FnName = "LAMP_init";

      LdStCallCounter& lscnts = getAnalysis<LdStCallCounter>();

      unsigned int cnt = lscnts.getCountInsts();
      unsigned int lps = lscnts.num_loops;

      //DOUT << "LAMP-- Ld/St/Call Count:" << cnt << " Loop Count:" << lps <<std::endl;

      Constant *InitFn = M.getOrInsertFunction(FnName, llvm::Type::getVoidTy(M.getContext()), llvm::Type::getInt32Ty(M.getContext()), llvm::Type::getInt32Ty(M.getContext()), llvm::Type::getInt64Ty(M.getContext()), llvm::Type::getInt64Ty(M.getContext()),(Type *)0);
      BasicBlock& entry = F.getEntryBlock();
      BasicBlock::iterator InsertPos = entry.begin();
      while (isa<AllocaInst>(InsertPos)) ++InsertPos;

      std::vector<Value*> Args(4);
      Args[0] = ConstantInt::get(llvm::Type::getInt32Ty(M.getContext()), cnt, false);
      Args[1] = ConstantInt::get(llvm::Type::getInt32Ty(M.getContext()), lps, false);
      Args[2] = ConstantInt::get(llvm::Type::getInt64Ty(M.getContext()), 1, false);
      Args[3] = ConstantInt::get(llvm::Type::getInt64Ty(M.getContext()), 0, false);

      CallInst::Create(InitFn, Args.begin(), Args.end(), "", InsertPos);														
      return true;
    }
  }
  return false;
}

// Loop instrumentation class instruments loops with invocation, iteration beginning, iteration ending
// and loop exiting calls.  It also counts the number of loops for use by LAMPProfiler initilization.
namespace {
  class LAMPLoopProfiler : public LoopPass {
    bool runOnLoop (Loop *Lp, LPPassManager &LPM);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequiredTransitive<LdStCallCounter>();
      //AU.addRequired<LAMPProfiler>();	For reasons incomprehensible to us, this is not permissible
    }

    unsigned int numLoops;			// numLoops for LAMPProfiler initilization

    public:
    bool doInitialization(Loop *Lp, LPPassManager &LPM) { return false; }
    static char ID;
    static bool IDInitFlag;
    static unsigned int loop_id;		// ids will be progressive starting after instruction ids
    LAMPLoopProfiler() : LoopPass(ID) 
    {  
      numLoops = 0;
    }	 
    unsigned int getNumLoops(){ return numLoops;}
  };
}

char LAMPLoopProfiler::ID = 0;
bool LAMPLoopProfiler::IDInitFlag = false;
unsigned int LAMPLoopProfiler::loop_id = 0;

static RegisterPass<LAMPLoopProfiler>
W("insert-lamp-loop-profiling",
    "Insert instrumentation for LAMP loop profiling");

LoopPass *llvm::createLAMPLoopProfilerPass() { return new LAMPLoopProfiler(); }


bool LAMPLoopProfiler::runOnLoop(Loop *Lp, LPPassManager &LPM) {
  BasicBlock *preHeader;
  BasicBlock *header;
  BasicBlock *latch;

  LdStCallCounter& lscnts = getAnalysis<LdStCallCounter>();

  if(!IDInitFlag)
  {
    loop_id = lscnts.getCountInsts()-1;	// first id will begin after instruction ids
    IDInitFlag = true;
  }

  SmallVector<BasicBlock*, 8> exitBlocks;			// assuming max 8 exit blocks.  Is this wise?
  // TRM 7/24/08 removed exiting blocks instrumentation
  // in favor of placing iter end prior loop exit
  header = Lp->getHeader();
  preHeader = Lp->getLoopPreheader();
  latch = Lp->getLoopLatch();

  Lp->getExitBlocks(exitBlocks);

  Module *M = (header->getParent())->getParent();

  numLoops++;

  lscnts.num_loops = numLoops;

  // insert invocation function at end of preheader (called once prior to loop)
  const char* InvocName = "LAMP_loop_invocation";
  Constant *InvocFn = M->getOrInsertFunction(InvocName, llvm::Type::getVoidTy(M->getContext()), llvm::Type::getInt32Ty(M->getContext()), (Type *)0);
  std::vector<Value*> Args(1);
  Args[0] = ConstantInt::get(llvm::Type::getInt32Ty(M->getContext()), ++loop_id);


  if (!preHeader->empty())

    CallInst::Create(InvocFn, Args.begin(), Args.end(), "", (preHeader->getTerminator()));
  else
    CallInst::Create(InvocFn, Args.begin(), Args.end(), "", (preHeader));


  // insert iteration begin function at beginning of header (called each loop)
  const char* IterBeginName = "LAMP_loop_iteration_begin";
  Constant *IterBeginFn = M->getOrInsertFunction(IterBeginName, llvm::Type::getVoidTy(M->getContext()), (Type *)0);	

  // find insertion point (after PHI nodes) -KF 11/18/2008
  for (BasicBlock::iterator ii = header->begin(), ie = header->end(); ii != ie; ++ii) {
    if (!isa<PHINode>(ii)) {
      CallInst::Create(IterBeginFn, "", ii);
      break;
    }
  }

  // insert iteration at cannonical backedge.  exiting block insertions removed in favor of exit block
  const char* IterEndName = "LAMP_loop_iteration_end";
  Constant *IterEndFn = M->getOrInsertFunction(IterEndName, llvm::Type::getVoidTy(M->getContext()), (Type *)0);	

  // cannonical backedge
  if (!latch->empty())
    CallInst::Create(IterEndFn, "", (latch->getTerminator()));
  else
    CallInst::Create(IterEndFn, "", (latch));


  // insert loop end at beginning of exit blocks
  const char* LoopEndName = "LAMP_loop_exit";
  Constant *LoopEndFn = M->getOrInsertFunction(LoopEndName, llvm::Type::getVoidTy(M->getContext()), (Type *)0);	

  set <BasicBlock*> BBSet; 
  BBSet.clear();
  for(unsigned int i = 0; i != exitBlocks.size(); i++){		
    // this ordering places iteration end before loop exit
    // make sure not inserting the same exit block more than once for a loop -PC 2/5/2009
    if (BBSet.find(exitBlocks[i])!=BBSet.end()) continue;
    BBSet.insert(exitBlocks[i]);
    // find insertion point (after PHI nodes) -PC 2/2/2009  -TODO: there is some function to do this.
    BasicBlock::iterator ii =  exitBlocks[i]->begin();
    while (isa<PHINode>(ii)) { ii++; }
    CallInst::Create(IterEndFn, "", ii);	// iter end placed before exit call
    CallInst::Create(LoopEndFn, "", ii);	// loop exiting
  }

  //DOUT << "Num Loops Processed: " << numLoops << "  Loop ID: " << loop_id << std::endl;
  return true;	
}
