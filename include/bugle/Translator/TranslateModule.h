#ifndef BUGLE_TRANSLATOR_TRANSLATEMODULE_H
#define BUGLE_TRANSLATOR_TRANSLATEMODULE_H

#include "bugle/Ref.h"
#include "bugle/Type.h"
#include "klee/util/GetElementPtrTypeIterator.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Target/TargetData.h"
#include <functional>
#include <set>

namespace llvm {

class Constant;
class GlobalVariable;
class Module;
class PointerType;

}

namespace bugle {

class Expr;
class Function;
class GlobalArray;
class Module;

class TranslateModule {
public:
  enum SourceLanguage {
    SL_C,
    SL_CUDA,
    SL_OpenCL,

    SL_Count
  };

private:
  bugle::Module *BM;
  llvm::Module *M;
  llvm::TargetData TD;
  SourceLanguage SL;

  llvm::DenseMap<llvm::Function *, bugle::Function *> FunctionMap;
  llvm::DenseMap<llvm::Constant *, ref<Expr>> ConstantMap;

  std::set<std::string> GPUEntryPoints;

  llvm::DenseMap<GlobalArray *, llvm::Value *> GlobalValueMap;

  bool NeedAdditionalByteArrayModels;
  std::set<llvm::Value *> ModelAsByteArray;
  bool ModelAllAsByteArray;

  void translateGlobalInit(GlobalArray *GA, unsigned Offset,
                           llvm::Constant *Init);
  GlobalArray *translateGlobalVariable(llvm::GlobalVariable *GV);
  void addGlobalArrayAttribs(GlobalArray *GA, llvm::PointerType *PT);
  bugle::GlobalArray *addGlobalArray(llvm::Value *V);

  ref<Expr> translateConstant(llvm::Constant *C);
  ref<Expr> doTranslateConstant(llvm::Constant *C);

  Type translateType(llvm::Type *T);
  Type translateArrayRangeType(llvm::Type *T);

  ref<Expr> translateGEP(ref<Expr> Ptr,
                         klee::gep_type_iterator begin,
                         klee::gep_type_iterator end,
                         std::function<ref<Expr>(llvm::Value *)> xlate);
  ref<Expr> translateBitCast(llvm::Type *SrcTy, llvm::Type *DestTy,
                             ref<Expr> Op);
  ref<Expr> translateUndef(Type t);

public:
  TranslateModule(llvm::Module *M, SourceLanguage SL) :
    BM(0), M(M), TD(M), SL(SL),
    NeedAdditionalByteArrayModels(false),
    ModelAllAsByteArray(false) {}
  void addGPUEntryPoint(llvm::StringRef Name);
  void translate();
  bugle::Module *takeModule() { return BM; }

  friend class TranslateFunction;
};

}

#endif
