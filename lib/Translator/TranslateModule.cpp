#include "bugle/Translator/TranslateModule.h"
#include "bugle/Translator/TranslateFunction.h"
#include "bugle/Expr.h"
#include "llvm/Constant.h"
#include "llvm/Constants.h"
#include "llvm/Module.h"
#include "llvm/Type.h"

using namespace llvm;
using namespace bugle;

ref<Expr> TranslateModule::translateConstant(Constant *C) {
  if (ConstantInt *CI = dyn_cast<ConstantInt>(C))
    return BVConstExpr::create(CI->getValue());
  if (ConstantFP *CF = dyn_cast<ConstantFP>(C))
    return BVToFloatExpr::create(
             BVConstExpr::create(CF->getValueAPF().bitcastToAPInt()));
  assert(0 && "Unhandled constant");
}

bugle::Type TranslateModule::translateType(llvm::Type *T) {
  Type::Kind K;
  if (T->isFloatingPointTy())
    K = Type::Float;
  else if (T->isPointerTy())
    K = Type::Pointer;
  else
    K = Type::BV;

  return Type(K, TD.getTypeSizeInBits(T));
}

void TranslateModule::translate() {
  llvm::Function *F = M->getFunction("main");
  TranslateFunction TF(this, F);
  TF.translate();
}
