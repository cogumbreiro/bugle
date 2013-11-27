#include "bugle/Preprocessing/CycleDetectPass.h"
#include "bugle/util/ErrorReporter.h"
#include "llvm/Pass.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallGraph.h"

using namespace llvm;
using namespace bugle;

bool CycleDetectPass::runOnModule(llvm::Module &M) {
#if LLVM_VERSION_MAJOR > 3 || (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR > 4)
  CallGraphNode* N = getAnalysis<CallGraphWrapperPass>().getRoot();
#else
  CallGraphNode* N = getAnalysis<CallGraph>().getRoot();
#endif
  scc_iterator<CallGraphNode*> i = scc_begin(N), e = scc_end(N);
  while (i != e) {
    if (i.hasLoop()) {
      ErrorReporter::reportFatalError(
                                  "Cannot inline, detected cycle in callgraph");
    }
    ++i;
  }

  return false;
}

char CycleDetectPass::ID = 0;
