#include "bugle/Translator/TranslateFunction.h"
#include "bugle/Translator/TranslateModule.h"
#include "bugle/BPLFunctionWriter.h"
#include "bugle/BPLModuleWriter.h"
#include "bugle/BasicBlock.h"
#include "bugle/Expr.h"
#include "bugle/GlobalArray.h"
#include "bugle/Module.h"
#include "llvm/BasicBlock.h"
#include "llvm/Function.h"
#include "llvm/InstrTypes.h"
#include "llvm/Instructions.h"
#include "llvm/Support/raw_ostream.h"

using namespace bugle;
using namespace llvm;

void TranslateFunction::translate() {
  for (auto i = F->arg_begin(), e = F->arg_end(); i != e; ++i) {
    Var *V = BF->addArgument(TM->translateType(i->getType()), i->getName());
    ValueExprMap[&*i] = VarRefExpr::create(V);
  }

  auto RT = F->getFunctionType()->getReturnType();
  if (!RT->isVoidTy())
    ReturnVar = BF->addReturn(TM->translateType(RT), "ret");

  for (auto i = F->begin(), e = F->end(); i != e; ++i)
    BasicBlockMap[&*i] = BF->addBasicBlock(i->getName());

  for (auto i = F->begin(), e = F->end(); i != e; ++i)
    translateBasicBlock(BasicBlockMap[&*i], &*i);
}

ref<Expr> TranslateFunction::translateValue(llvm::Value *V) {
  if (isa<Instruction>(V) || isa<Argument>(V)) {
    auto MI = ValueExprMap.find(V);
    assert(MI != ValueExprMap.end());
    return MI->second;
  }

  if (auto C = dyn_cast<Constant>(V))
    return TM->translateConstant(C);

  assert(0 && "Unsupported value");
}

void TranslateFunction::translateInstruction(bugle::BasicBlock *BBB,
                                             Instruction *I) {
  ref<Expr> E;
  if (auto BO = dyn_cast<BinaryOperator>(I)) {
    ref<Expr> LHS = translateValue(BO->getOperand(0)),
              RHS = translateValue(BO->getOperand(1));
    switch (BO->getOpcode()) {
    case BinaryOperator::Add:
      E = BVAddExpr::create(LHS, RHS);
    default:
      assert(0 && "Unsupported binary operator");
    }
  } else if (auto AI = dyn_cast<AllocaInst>(I)) {
    GlobalArray *GA = TM->BM->addGlobal(AI->getName());
    E = PointerExpr::create(GlobalArrayRefExpr::create(GA),
                        BVConstExpr::createZero(TM->TD.getPointerSizeInBits()));
  } else if (auto SI = dyn_cast<StoreInst>(I)) {
    ref<Expr> Ptr = translateValue(SI->getPointerOperand()),
              Val = translateValue(SI->getValueOperand());
    BBB->addStmt(new StoreStmt(Ptr, Val));
    return;
  } else if (auto RI = dyn_cast<ReturnInst>(I)) {
    if (auto V = RI->getReturnValue()) {
      assert(ReturnVar && "Returning value without return variable?");
      ref<Expr> Val = translateValue(V);
      BBB->addStmt(new VarAssignStmt(ReturnVar, Val));
    }
    BBB->addStmt(new ReturnStmt);
    return;
  } else {
    assert(0 && "Unsupported instruction");
  }
  ValueExprMap[I] = E;
  BBB->addStmt(new EvalStmt(E));
}

void TranslateFunction::translateBasicBlock(bugle::BasicBlock *BBB,
                                            llvm::BasicBlock *BB) {
  for (auto i = BB->begin(), e = BB->end(); i != e; ++i)
    translateInstruction(BBB, &*i);
}
