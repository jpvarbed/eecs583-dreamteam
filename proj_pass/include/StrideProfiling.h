#ifndef LLVM_TRANSFORMS_STRIDEINSTRUMENTATION_H
#define LLVM_TRANSFORMS_STRIDEINSTRUMENTATION_H

namespace llvm {

  class ModulePass;
  class FunctionPass;
  class LoopPass;

  ModulePass *createStrideInitPass();
  ModulePass* createLdStCallCounter();
  FunctionPass *createStrideProfilerPass();

}

#endif
