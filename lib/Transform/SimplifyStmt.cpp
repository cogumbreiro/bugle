#include "bugle/Transform/SimplifyStmt.h"
#include "bugle/Module.h"
#include "bugle/Function.h"
#include "bugle/BasicBlock.h"

using namespace bugle;

namespace {

bool hasSideEffects(Expr *e) {
  return isa<CallExpr>(e) || isa<HavocExpr>(e) || 
    isa<ArraySnapshotExpr>(e) || isa<AddNoovflExpr>(e) || isa<AtomicExpr>(e);
}

bool isTemporal(Expr *e) {
  if(auto LE = dyn_cast<LoadExpr>(e)) {
    return LE->getIsTemporal();
  }
  return isa<HavocExpr>(e) || isa<ArraySnapshotExpr>(e) || isa<AtomicExpr>(e);
}

bool isNullPointerLoad(Expr *e) {
  if(auto LE = dyn_cast<LoadExpr>(e)) {
    return LE->getArray()->getType().range() == Type::Any;
  }
  return false;
}

void ProcessBasicBlock(BasicBlock *BB) {
  OwningPtrVector<Stmt> &V = BB->getStmtVector();
  if (V.empty())
    return;
  for (auto i = V.end()-1;;) {
    if (auto ES = dyn_cast<EvalStmt>(*i)) {
      Expr *E = ES->getExpr().get();
      if (hasSideEffects(E)) {
        if (i == V.begin())
          break;
        --i;
        continue;
      }

      if ((E->refCount == 1 && !isNullPointerLoad(E)) || (!isTemporal(E) && E->refCount <= 2)) {
        auto ii = i;
        bool begin = false;
        if (i == V.begin())
          begin = true;
        else
          --i;
        delete *ii;
        V.erase(ii);
        if (begin)
          break;
        else
          continue;
      }
    }
    if (i == V.begin())
      break;
    --i;
  }
}

void ProcessFunction(Function *F) {
  for (auto i = F->begin(), e = F->end(); i != e; ++i)
    ProcessBasicBlock(*i);
}

void ProcessModule(Module *M) {
  for (auto i = M->begin(), e = M->end(); i != e; ++i)
    ProcessFunction(*i);
}

}

void bugle::simplifyStmt(Module *M) {
  ProcessModule(M);
}
