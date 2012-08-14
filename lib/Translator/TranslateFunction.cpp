#include "bugle/Translator/TranslateFunction.h"
#include "bugle/Translator/TranslateModule.h"
#include "bugle/BPLFunctionWriter.h"
#include "bugle/BPLModuleWriter.h"
#include "bugle/BasicBlock.h"
#include "bugle/Expr.h"
#include "bugle/GlobalArray.h"
#include "bugle/Module.h"
#include "bugle/util/Functional.h"
#include "llvm/BasicBlock.h"
#include "llvm/DebugInfo.h"
#include "llvm/Function.h"
#include "llvm/InstrTypes.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Intrinsics.h"
#include "llvm/Metadata.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "klee/util/GetElementPtrTypeIterator.h"

using namespace bugle;
using namespace llvm;

static cl::opt<bool>
DumpTranslatedExprs("dump-translated-exprs", cl::Hidden, cl::init(false),
  cl::desc("Dump each translated expression below the instruction which "
           "generated it"));

TranslateFunction::SpecialFnMapTy
  TranslateFunction::SpecialFunctionMaps[TranslateModule::SL_Count];

// Appends at least the given basic block to the given list BBList (if not
// already present), so as to maintain the invariants that:
//  1) Each element of BBList is also a member of BBSet and vice versa;
//  2) Each element of BBList with a single predecessor must appear after
//     that predecessor.
// This invariant is important when translating basic blocks so that we do not
// see a use of an instruction in a basic block other than that currently
// being processed (i.e., in a phi node) before its definition.
static void AddBasicBlockInOrder(std::set<llvm::BasicBlock *> &BBSet,
                                 std::vector<llvm::BasicBlock *> &BBList,
                                 llvm::BasicBlock *BB) {
  if (BBSet.find(BB) != BBSet.end())
    return;

  // If the basic block has one predecessor, ...
  auto PredB = pred_begin(BB), PredI = PredB, PredE = pred_end(BB);
  if (PredI != PredE) {
    ++PredI;
    if (PredI == PredE) {
      // ... add that predecessor first.
      AddBasicBlockInOrder(BBSet, BBList, *PredB);
    }
  }

  BBSet.insert(BB);
  BBList.push_back(BB);
}

bool TranslateFunction::isSpecialFunction(TranslateModule::SourceLanguage SL,
                                          const std::string &fnName) {
  SpecialFnMapTy &SpecialFunctionMap = initSpecialFunctionMap(SL);
  return SpecialFunctionMap.Functions.find(fnName) !=
         SpecialFunctionMap.Functions.end();
}

TranslateFunction::SpecialFnMapTy &
TranslateFunction::initSpecialFunctionMap(TranslateModule::SourceLanguage SL) {
  SpecialFnMapTy &SpecialFunctionMap = SpecialFunctionMaps[SL];
  if (SpecialFunctionMap.Functions.empty()) {
    auto &fns = SpecialFunctionMap.Functions;
    fns["llvm.lifetime.start"] = &TranslateFunction::handleNoop;
    fns["llvm.lifetime.end"] = &TranslateFunction::handleNoop;
    fns["bugle_assert"] = &TranslateFunction::handleAssert;
    fns["__assert"] = &TranslateFunction::handleAssert;
    fns["__invariant"] = &TranslateFunction::handleAssert;
    fns["__global_assert"] = &TranslateFunction::handleGlobalAssert;
    fns["bugle_assume"] = &TranslateFunction::handleAssume;
    fns["__assert_fail"] = &TranslateFunction::handleAssertFail;
    fns["bugle_requires"] = &TranslateFunction::handleRequires;
    fns["__requires"] = &TranslateFunction::handleRequires;
    fns["bugle_ensures"] = &TranslateFunction::handleEnsures;
    fns["__ensures"] = &TranslateFunction::handleEnsures;
    fns["__return_val_int"] = &TranslateFunction::handleReturnVal;
    fns["__return_val_int4"] = &TranslateFunction::handleReturnVal;
    fns["__return_val_bool"] = &TranslateFunction::handleReturnVal;
    fns["__old_int"] = &TranslateFunction::handleOld;
    fns["__old_bool"] = &TranslateFunction::handleOld;
    fns["__other_int"] = &TranslateFunction::handleOtherInt;
    fns["__other_bool"] = &TranslateFunction::handleOtherBool;
    fns["__other_ptr_base"] = &TranslateFunction::handleOtherPtrBase;
    fns["__implies"] = &TranslateFunction::handleImplies;
    fns["__enabled"] = &TranslateFunction::handleEnabled;
    fns["__read_local"] = &TranslateFunction::handleReadHasOccurred;
    fns["__read_global"] = &TranslateFunction::handleReadHasOccurred;
    fns["__write_local"] = &TranslateFunction::handleWriteHasOccurred;
    fns["__write_global"] = &TranslateFunction::handleWriteHasOccurred;
    fns["__read_offset_local"] = &TranslateFunction::handleReadOffset;
    fns["__read_offset_global"] = &TranslateFunction::handleReadOffset;
    fns["__write_offset_local"] = &TranslateFunction::handleWriteOffset;
    fns["__write_offset_global"] = &TranslateFunction::handleWriteOffset;
    fns["__ptr_base_local"] = &TranslateFunction::handlePtrBase;
    fns["__ptr_base_global"] = &TranslateFunction::handlePtrBase;
    fns["__ptr_offset_local"] = &TranslateFunction::handlePtrOffset;
    fns["__ptr_offset_global"] = &TranslateFunction::handlePtrOffset;
    if (SL == TranslateModule::SL_OpenCL) {
      fns["get_local_id"] = &TranslateFunction::handleGetLocalId;
      fns["get_group_id"] = &TranslateFunction::handleGetGroupId;
      fns["get_local_size"] = &TranslateFunction::handleGetLocalSize;
      fns["get_num_groups"] = &TranslateFunction::handleGetNumGroups;
    }

    auto &ints = SpecialFunctionMap.Intrinsics;
    ints[Intrinsic::cos] = &TranslateFunction::handleCos;
    ints[Intrinsic::exp2] = &TranslateFunction::handleExp;
    ints[Intrinsic::fabs] = &TranslateFunction::handleFabs;
    ints[Intrinsic::fma] = &TranslateFunction::handleFma;
    ints[Intrinsic::log2] = &TranslateFunction::handleLog;
    ints[Intrinsic::pow] = &TranslateFunction::handlePow;
    ints[Intrinsic::sin] = &TranslateFunction::handleSin;
    ints[Intrinsic::sqrt] = &TranslateFunction::handleSqrt;
    ints[Intrinsic::dbg_value] = &TranslateFunction::handleNoop;
    ints[Intrinsic::dbg_declare] = &TranslateFunction::handleNoop;
  }
  return SpecialFunctionMap;
}

void TranslateFunction::translate() {
  initSpecialFunctionMap(TM->SL);

  if (isGPUEntryPoint || F->getName() == "main")
    BF->setEntryPoint(true);

  if (isGPUEntryPoint)
    BF->addAttribute("kernel");

  if (TM->SL == TranslateModule::SL_OpenCL) {
    if (F->getName() == "barrier")
      BF->addAttribute("barrier");
  }

  unsigned PtrSize = TM->TD.getPointerSizeInBits();
  for (auto i = F->arg_begin(), e = F->arg_end(); i != e; ++i) {
    if (isGPUEntryPoint && i->getType()->isPointerTy()) {
      GlobalArray *GA = TM->getGlobalArray(&*i);
      ValueExprMap[&*i] = PointerExpr::create(GlobalArrayRefExpr::create(GA),
                        BVConstExpr::createZero(PtrSize));
    } else {
      Var *V = BF->addArgument(TM->getModelledType(&*i), i->getName());
      ValueExprMap[&*i] = TM->unmodelValue(&*i, VarRefExpr::create(V));
    }
  }

  if (BF->return_begin() != BF->return_end())
    ReturnVar = *BF->return_begin();

  std::set<llvm::BasicBlock *> BBSet;
  std::vector<llvm::BasicBlock *> BBList;

  for (auto i = F->begin(), e = F->end(); i != e; ++i) {
    AddBasicBlockInOrder(BBSet, BBList, &*i);
    BasicBlockMap[&*i] = BF->addBasicBlock(i->getName());
  }

  for (auto i = BBList.begin(), e = BBList.end(); i != e; ++i)
    translateBasicBlock(BasicBlockMap[*i], *i);

  // If we're modelling everything as a byte array, don't bother to compute
  // value models.
  if (TM->ModelAllAsByteArray)
    return;

  // For each phi we encountered in the function, see if we can model it.
  for (auto i = PhiAssignsMap.begin(), e = PhiAssignsMap.end(); i != e; ++i) {
    TM->computeValueModel(i->first, PhiVarMap[i->first], i->second);
  }

  // See if we can model the return value.
  TM->computeValueModel(F, 0, ReturnVals);
}

ref<Expr> TranslateFunction::translateValue(llvm::Value *V) {
  if (isa<Instruction>(V) || isa<Argument>(V)) {
    auto MI = ValueExprMap.find(V);
    assert(MI != ValueExprMap.end());
    return MI->second;
  }

  if (auto C = dyn_cast<Constant>(V))
    return TM->translateConstant(C);

  if (isa<MDNode>(V)) {
    // ignore metadata values
    return 0;
  }
  assert(0 && "Unsupported value");
  return 0;
}

Var *TranslateFunction::getPhiVariable(llvm::PHINode *PN) {
  auto &i = PhiVarMap[PN];
  if (i)
    return i;

  i = BF->addLocal(TM->getModelledType(PN), PN->getName());
  return i;
}

void TranslateFunction::addPhiAssigns(bugle::BasicBlock *BBB,
                                      llvm::BasicBlock *Pred,
                                      llvm::BasicBlock *Succ) {
  std::vector<Var *> Vars;
  std::vector<ref<Expr>> Exprs;
  for (auto i = Succ->begin(), e = Succ->end(); i != e && isa<PHINode>(i); ++i){
    PHINode *PN = cast<PHINode>(i);
    int idx = PN->getBasicBlockIndex(Pred);
    assert(idx != -1 && "No phi index?");

    Vars.push_back(getPhiVariable(PN));
    auto Val = TM->modelValue(PN, translateValue(PN->getIncomingValue(idx)));
    Exprs.push_back(Val);
    PhiAssignsMap[PN].push_back(Val);
  }

  if (!Vars.empty())
    BBB->addStmt(new VarAssignStmt(Vars, Exprs));
}

void TranslateFunction::addLocToStmt(Stmt *stmt, llvm::Instruction *I) {
  stmt->setSourceLoc(extractSourceLoc(I));
}

SourceLoc *TranslateFunction::extractSourceLoc(llvm::Instruction *I) {
  SourceLoc* sourceloc = 0;
  if (llvm::MDNode *mdnode = I->getMetadata("dbg")) {
    llvm::DILocation Loc(mdnode);
    sourceloc = new SourceLoc(Loc.getLineNumber(),
                              Loc.getColumnNumber(),
                              Loc.getFilename().str(),
                              Loc.getDirectory().str());
  }
  return sourceloc;
}


ref<Expr> TranslateFunction::handleNoop(bugle::BasicBlock *BBB,
                                        llvm::CallInst *CI,
                                        const std::vector<ref<Expr>> &Args) {
  return 0;
}

ref<Expr> TranslateFunction::handleAssert(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  Stmt *assertstmt = new AssertStmt(Expr::createNeZero(Args[0]));
  addLocToStmt(assertstmt, CI);
  BBB->addStmt(assertstmt);
  return 0;
}

ref<Expr> TranslateFunction::handleAssertFail(bugle::BasicBlock *BBB,
                                           llvm::CallInst *CI,
                                           const std::vector<ref<Expr>> &Args) {
  Stmt *assertstmt = new AssertStmt(BoolConstExpr::create(false));
  addLocToStmt(assertstmt, CI);
  BBB->addStmt(assertstmt);
  return 0;
}

ref<Expr> TranslateFunction::handleAssume(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  BBB->addStmt(new AssumeStmt(Expr::createNeZero(Args[0])));
  return 0;
}

ref<Expr> TranslateFunction::handleGlobalAssert(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  BBB->addStmt(new GlobalAssertStmt(Expr::createNeZero(Args[0])));
  return 0;
}

ref<Expr> TranslateFunction::handleRequires(bugle::BasicBlock *BBB,
                                           llvm::CallInst *CI,
                                           const std::vector<ref<Expr>> &Args) {
  BF->addRequires(Expr::createNeZero(Args[0]));
  return 0;
}

ref<Expr> TranslateFunction::handleEnsures(bugle::BasicBlock *BBB,
                                           llvm::CallInst *CI,
                                           const std::vector<ref<Expr>> &Args) {
  BF->addEnsures(Expr::createNeZero(Args[0]));
  return 0;
}

ref<Expr> TranslateFunction::handleOld(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  return OldExpr::create(Args[0]);
}

ref<Expr> TranslateFunction::handleReturnVal(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  return VarRefExpr::create(ReturnVar);
}

ref<Expr> TranslateFunction::handleOtherInt(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  return OtherIntExpr::create(Args[0]);
}

ref<Expr> TranslateFunction::handleOtherBool(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  return BoolToBVExpr::create(OtherBoolExpr::create(BVToBoolExpr::create(Args[0])));
}

ref<Expr> TranslateFunction::handleOtherPtrBase(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  return OtherPtrBaseExpr::create(Args[0]);
}

ref<Expr> TranslateFunction::handleImplies(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  return BoolToBVExpr::create(ImpliesExpr::create(BVToBoolExpr::create(Args[0]), BVToBoolExpr::create(Args[1])));
}

ref<Expr> TranslateFunction::handleEnabled(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  return BoolToBVExpr::create(SpecialVarRefExpr::create(bugle::Type(bugle::Type::Bool), "__enabled"));
}

ref<Expr> TranslateFunction::handleReadHasOccurred(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  return BoolToBVExpr::create(AccessHasOccurredExpr::create(
                              ArrayIdExpr::create(Args[0], TM->defaultRange()),
                              false));
}

ref<Expr> TranslateFunction::handleWriteHasOccurred(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  return BoolToBVExpr::create(AccessHasOccurredExpr::create(
                              ArrayIdExpr::create(Args[0], TM->defaultRange()),
                              true));
}

ref<Expr> TranslateFunction::handleReadOffset(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  ref<Expr> arrayIdExpr = ArrayIdExpr::create(Args[0], TM->defaultRange());
  ref<Expr> result = AccessOffsetExpr::create(arrayIdExpr, false);
  Type range = arrayIdExpr->getType().range();

  if(range.isKind(Type::BV) || range.isKind(Type::Float)) {
    if(range.width > 8) {
      result = BVMulExpr::create(BVConstExpr::create(
                                 TM->TD.getPointerSizeInBits(),
                                 range.width/8), result);
    }
  }
  return result;
}

ref<Expr> TranslateFunction::handleWriteOffset(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  ref<Expr> arrayIdExpr = ArrayIdExpr::create(Args[0], TM->defaultRange());
  ref<Expr> result = AccessOffsetExpr::create(arrayIdExpr, true);
  Type range = arrayIdExpr->getType().range();

  if(range.isKind(Type::BV) || range.isKind(Type::Float)) {
    if(range.width > 8) {
      result = BVMulExpr::create(BVConstExpr::create(
                                 TM->TD.getPointerSizeInBits(),
                                 range.width/8), result);
    }
  }
  return result;
}

ref<Expr> TranslateFunction::handlePtrOffset(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  return ArrayOffsetExpr::create(Args[0]);
}

ref<Expr> TranslateFunction::handlePtrBase(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  return ArrayIdExpr::create(Args[0], TM->defaultRange());
}

static std::string mkDimName(const std::string &prefix, ref<Expr> dim) {
  auto CE = dyn_cast<BVConstExpr>(dim);
  switch (CE->getValue().getZExtValue()) {
  case 0: return prefix + "_x";
  case 1: return prefix + "_y";
  case 2: return prefix + "_z";
  default: assert(0 && "Unsupported dimension!"); return 0;
  }
}

static ref<Expr> mkLocalId(bugle::Type t, ref<Expr> dim) {
  return SpecialVarRefExpr::create(t, mkDimName("local_id", dim));
}

static ref<Expr> mkGroupId(bugle::Type t, ref<Expr> dim) {
  return SpecialVarRefExpr::create(t, mkDimName("group_id", dim));
}

static ref<Expr> mkLocalSize(bugle::Type t, ref<Expr> dim) {
  return SpecialVarRefExpr::create(t, mkDimName("group_size", dim));
}

static ref<Expr> mkNumGroups(bugle::Type t, ref<Expr> dim) {
  return SpecialVarRefExpr::create(t, mkDimName("num_groups", dim));
}

ref<Expr> TranslateFunction::handleGetLocalId(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  Type t = TM->translateType(CI->getType());
  return mkLocalId(t, Args[0]);
}

ref<Expr> TranslateFunction::handleGetGroupId(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  Type t = TM->translateType(CI->getType());
  return mkGroupId(t, Args[0]);
}

ref<Expr> TranslateFunction::handleGetLocalSize(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  Type t = TM->translateType(CI->getType());
  return mkLocalSize(t, Args[0]);
}

ref<Expr> TranslateFunction::handleGetNumGroups(bugle::BasicBlock *BBB,
                                          llvm::CallInst *CI,
                                          const std::vector<ref<Expr>> &Args) {
  Type t = TM->translateType(CI->getType());
  return mkNumGroups(t, Args[0]);
}

ref<Expr> TranslateFunction::handleCos(bugle::BasicBlock *BBB,
                                        llvm::CallInst *CI,
                                        const std::vector<ref<Expr>> &Args) {
  llvm::Type *Ty = CI->getType();
  return maybeTranslateSIMDInst(BBB, Ty, Ty, Args[0],
                                [&](llvm::Type *T, ref<Expr> E) {
    return FCosExpr::create(E);
  });
}

ref<Expr> TranslateFunction::handleExp(bugle::BasicBlock *BBB,
                                       llvm::CallInst *CI,
                                       const std::vector<ref<Expr>> &Args) {
  llvm::Type *Ty = CI->getType();
  return maybeTranslateSIMDInst(BBB, Ty, Ty, Args[0],
                                [&](llvm::Type *T, ref<Expr> E) {
    return FExpExpr::create(E);
  });
}

ref<Expr> TranslateFunction::handleFabs(bugle::BasicBlock *BBB,
                                        llvm::CallInst *CI,
                                        const std::vector<ref<Expr>> &Args) {
  llvm::Type *Ty = CI->getType();
  return maybeTranslateSIMDInst(BBB, Ty, Ty, Args[0],
                                [&](llvm::Type *T, ref<Expr> E) {
    return FAbsExpr::create(E);
  });
}

ref<Expr> TranslateFunction::handleFma(bugle::BasicBlock *BBB,
                                       llvm::CallInst *CI,
                                       const std::vector<ref<Expr>> &Args) {
  llvm::Type *Ty = CI->getType();
  ref<Expr> M =
    maybeTranslateSIMDInst(BBB, Ty, Ty, Args[0], Args[1], FMulExpr::create);
  return
    maybeTranslateSIMDInst(BBB, Ty, Ty, M, Args[2], FAddExpr::create);
}

ref<Expr> TranslateFunction::handleLog(bugle::BasicBlock *BBB,
                                       llvm::CallInst *CI,
                                       const std::vector<ref<Expr>> &Args) {
  llvm::Type *Ty = CI->getType();
  return maybeTranslateSIMDInst(BBB, Ty, Ty, Args[0],
                                [&](llvm::Type *T, ref<Expr> E) {
    return FLogExpr::create(E);
  });
}

ref<Expr> TranslateFunction::handlePow(bugle::BasicBlock *BBB,
                                       llvm::CallInst *CI,
                                       const std::vector<ref<Expr>> &Args) {
  llvm::Type *Ty = CI->getType();
  return maybeTranslateSIMDInst(BBB, Ty, Ty, Args[0], Args[1],
                                [&](ref<Expr> LHS, ref<Expr> RHS) {
    return FPowExpr::create(LHS, RHS);
  });
}

ref<Expr> TranslateFunction::handleSin(bugle::BasicBlock *BBB,
                                       llvm::CallInst *CI,
                                       const std::vector<ref<Expr>> &Args) {
  llvm::Type *Ty = CI->getType();
  return maybeTranslateSIMDInst(BBB, Ty, Ty, Args[0],
                                [&](llvm::Type *T, ref<Expr> E) {
    return FSinExpr::create(E);
  });
}

ref<Expr> TranslateFunction::handleSqrt(bugle::BasicBlock *BBB,
                                        llvm::CallInst *CI,
                                        const std::vector<ref<Expr>> &Args) {
  llvm::Type *Ty = CI->getType();
  return maybeTranslateSIMDInst(BBB, Ty, Ty, Args[0],
                                [&](llvm::Type *T, ref<Expr> E) {
    return FSqrtExpr::create(E);
  });
}

ref<Expr> TranslateFunction::maybeTranslateSIMDInst(bugle::BasicBlock *BBB,
                             llvm::Type *Ty, llvm::Type *OpTy,
                             ref<Expr> Op,
                          std::function<ref<Expr>(llvm::Type *, ref<Expr>)> F) {
  if (!isa<VectorType>(Ty))
    return F(Ty, Op);

  auto VT = cast<VectorType>(Ty), OpVT = cast<VectorType>(OpTy);
  unsigned NumElems = VT->getNumElements();
  assert(OpVT->getNumElements() == NumElems);
  unsigned ElemWidth = Op->getType().width / NumElems;
  std::vector<ref<Expr>> Elems;
  for (unsigned i = 0; i < NumElems; ++i) {
    ref<Expr> Opi = BVExtractExpr::create(Op, i*ElemWidth, ElemWidth);
    if (OpVT->getElementType()->isFloatingPointTy())
      Opi = BVToFloatExpr::create(Opi);
    ref<Expr> Elem = F(VT->getElementType(), Opi);
    BBB->addStmt(new EvalStmt(Elem));
    if (VT->getElementType()->isFloatingPointTy()) {
      Elem = FloatToBVExpr::create(Elem);
      BBB->addStmt(new EvalStmt(Elem));
    }
    Elems.push_back(Elem);
  }
  return Expr::createBVConcatN(Elems);
}

ref<Expr> TranslateFunction::maybeTranslateSIMDInst(bugle::BasicBlock *BBB,
                             llvm::Type *Ty, llvm::Type *OpTy,
                             ref<Expr> LHS, ref<Expr> RHS,
                             std::function<ref<Expr>(ref<Expr>, ref<Expr>)> F) {
  if (!isa<VectorType>(Ty))
    return F(LHS, RHS);

  auto VT = cast<VectorType>(Ty), OpVT = cast<VectorType>(OpTy);
  unsigned NumElems = VT->getNumElements();
  assert(OpVT->getNumElements() == NumElems);
  unsigned ElemWidth = LHS->getType().width / NumElems;
  std::vector<ref<Expr>> Elems;
  for (unsigned i = 0; i < NumElems; ++i) {
    ref<Expr> LHSi = BVExtractExpr::create(LHS, i*ElemWidth, ElemWidth);
    ref<Expr> RHSi = BVExtractExpr::create(RHS, i*ElemWidth, ElemWidth);
    if (OpVT->getElementType()->isFloatingPointTy()) {
      LHSi = BVToFloatExpr::create(LHSi);
      RHSi = BVToFloatExpr::create(RHSi);
    }
    ref<Expr> Elem = F(LHSi, RHSi);
    BBB->addStmt(new EvalStmt(Elem));
    if (VT->getElementType()->isFloatingPointTy()) {
      Elem = FloatToBVExpr::create(Elem);
      BBB->addStmt(new EvalStmt(Elem));
    }
    Elems.push_back(Elem);
  }
  return Expr::createBVConcatN(Elems);
}

void TranslateFunction::translateInstruction(bugle::BasicBlock *BBB,
                                             Instruction *I) {
  ref<Expr> E;
  if (auto BO = dyn_cast<BinaryOperator>(I)) {
    ref<Expr> LHS = translateValue(BO->getOperand(0)),
              RHS = translateValue(BO->getOperand(1));
    ref<Expr> (*F)(ref<Expr>, ref<Expr>);
    switch (BO->getOpcode()) {
    case BinaryOperator::Add:  F = BVAddExpr::create;  break;
    case BinaryOperator::FAdd: F = FAddExpr::create;   break;
    case BinaryOperator::Sub:  F = BVSubExpr::create;  break;
    case BinaryOperator::FSub: F = FSubExpr::create;   break;
    case BinaryOperator::Mul:  F = BVMulExpr::create;  break;
    case BinaryOperator::FMul: F = FMulExpr::create;   break;
    case BinaryOperator::SDiv: F = BVSDivExpr::create; break;
    case BinaryOperator::UDiv: F = BVUDivExpr::create; break;
    case BinaryOperator::FDiv: F = FDivExpr::create;   break;
    case BinaryOperator::SRem: F = BVSRemExpr::create; break;
    case BinaryOperator::URem: F = BVURemExpr::create; break;
    case BinaryOperator::Shl:  F = BVShlExpr::create;  break;
    case BinaryOperator::AShr: F = BVAShrExpr::create; break;
    case BinaryOperator::LShr: F = BVLShrExpr::create; break;
    case BinaryOperator::And:  F = BVAndExpr::create;  break;
    case BinaryOperator::Or:   F = BVOrExpr::create;   break;
    case BinaryOperator::Xor:  F = BVXorExpr::create;  break;
    default:
      assert(0 && "Unsupported binary operator");
    }
    E = maybeTranslateSIMDInst(BBB, BO->getType(), BO->getType(), LHS, RHS, F);
  } else if (auto GEPI = dyn_cast<GetElementPtrInst>(I)) {
    ref<Expr> Ptr = translateValue(GEPI->getPointerOperand());
    E = TM->translateGEP(Ptr, klee::gep_type_begin(GEPI),
                         klee::gep_type_end(GEPI),
                         [&](Value *V) { return translateValue(V); });
  } else if (auto AI = dyn_cast<AllocaInst>(I)) {
    GlobalArray *GA = TM->getGlobalArray(AI);
    E = PointerExpr::create(GlobalArrayRefExpr::create(GA),
                        BVConstExpr::createZero(TM->TD.getPointerSizeInBits()));
  } else if (auto LI = dyn_cast<LoadInst>(I)) {
    ref<Expr> Ptr = translateValue(LI->getPointerOperand()),
              PtrArr = ArrayIdExpr::create(Ptr, TM->defaultRange()),
              PtrOfs = ArrayOffsetExpr::create(Ptr);
    Type ArrRangeTy = PtrArr->getType().range();
    Type LoadTy = TM->translateType(LI->getType()), LoadElTy = LoadTy;
    auto VT = dyn_cast<VectorType>(LI->getType());
    if (VT)
      LoadElTy = TM->translateType(VT->getElementType());
    assert(LoadTy.width % 8 == 0);
    ref<Expr> Div;
    if ((ArrRangeTy == LoadElTy || ArrRangeTy == Type(Type::Any)) &&
        !(Div = Expr::createExactBVUDiv(PtrOfs, LoadElTy.width/8)).isNull()) {
      if (VT) {
        std::vector<ref<Expr>> ElemsLoaded;
        for (unsigned i = 0; i != VT->getNumElements(); ++i) {
          ref<Expr> ElemOfs =
            BVAddExpr::create(Div,
                              BVConstExpr::create(Div->getType().width, i));
          ref<Expr> ValElem = LoadExpr::create(PtrArr, ElemOfs);
          BBB->addStmt(new EvalStmt(ValElem));
          if (LoadElTy.isKind(Type::Pointer))
            ValElem = PtrToBVExpr::create(ValElem);
          else if (LoadElTy.isKind(Type::Float))
            ValElem = FloatToBVExpr::create(ValElem);
          ElemsLoaded.push_back(ValElem);
        }
        E = Expr::createBVConcatN(ElemsLoaded);
      } else {
        E = LoadExpr::create(PtrArr, Div);
      }
    } else if (ArrRangeTy == Type(Type::BV, 8)) {
      std::vector<ref<Expr> > BytesLoaded;
      for (unsigned i = 0; i != LoadTy.width / 8; ++i) {
        ref<Expr> PtrByteOfs =
          BVAddExpr::create(PtrOfs,
                            BVConstExpr::create(PtrOfs->getType().width, i));
        ref<Expr> ValByte = LoadExpr::create(PtrArr, PtrByteOfs);
        BytesLoaded.push_back(ValByte);
        BBB->addStmt(new EvalStmt(ValByte));
      }
      E = Expr::createBVConcatN(BytesLoaded);
      if (LoadTy.isKind(Type::Pointer))
        E = BVToPtrExpr::create(E);
      else if (LoadTy.isKind(Type::Float))
        E = BVToFloatExpr::create(E);
    } else {
      TM->NeedAdditionalByteArrayModels = true;
      std::set<GlobalArray *> Globals;
      if (PtrArr->computeArrayCandidates(Globals)) {
        std::transform(Globals.begin(), Globals.end(),
            std::inserter(TM->ModelAsByteArray, TM->ModelAsByteArray.begin()),
            [&](GlobalArray *A) { return TM->GlobalValueMap[A]; });
      } else {
        TM->NextModelAllAsByteArray = true;
      }
      E = TM->translateUndef(LoadTy);
    }
  } else if (auto SI = dyn_cast<StoreInst>(I)) {
    ref<Expr> Ptr = translateValue(SI->getPointerOperand()),
              Val = translateValue(SI->getValueOperand()),
              PtrArr = ArrayIdExpr::create(Ptr, TM->defaultRange()),
              PtrOfs = ArrayOffsetExpr::create(Ptr);
    Type ArrRangeTy = PtrArr->getType().range();
    Type StoreTy = Val->getType(), StoreElTy = StoreTy;
    auto VT = dyn_cast<VectorType>(SI->getValueOperand()->getType());
    if (VT)
      StoreElTy = TM->translateType(VT->getElementType());
    assert(StoreTy.width % 8 == 0);
    ref<Expr> Div;
    if (ArrRangeTy == StoreElTy &&
        !(Div = Expr::createExactBVUDiv(PtrOfs, StoreElTy.width/8)).isNull()) {
      if (VT) {
        for (unsigned i = 0; i != VT->getNumElements(); ++i) {
          ref<Expr> ElemOfs =
            BVAddExpr::create(Div,
                              BVConstExpr::create(Div->getType().width, i));
          ref<Expr> ValElem =
            BVExtractExpr::create(Val, i*StoreElTy.width, StoreElTy.width);
          if (StoreElTy.isKind(Type::Pointer))
            ValElem = BVToPtrExpr::create(ValElem);
          else if (StoreElTy.isKind(Type::Float))
            ValElem = BVToFloatExpr::create(ValElem);
          StoreStmt* SS = new StoreStmt(PtrArr, ElemOfs, ValElem);
          addLocToStmt(SS, I);
          BBB->addStmt(SS);
        }
      } else {
        StoreStmt* SS = new StoreStmt(PtrArr, Div, Val);
        addLocToStmt(SS, I);
        BBB->addStmt(SS);
      }
    } else if (ArrRangeTy == Type(Type::BV, 8)) {
      if (StoreTy.isKind(Type::Pointer)) {
        Val = PtrToBVExpr::create(Val);
        EvalStmt* ES = new EvalStmt(Val);
        addLocToStmt(ES, I);
        BBB->addStmt(ES);
      } else if (StoreTy.isKind(Type::Float)) {
        Val = FloatToBVExpr::create(Val);
        EvalStmt* ES = new EvalStmt(Val);
        addLocToStmt(ES, I);
        BBB->addStmt(ES);
      }
      for (unsigned i = 0; i != Val->getType().width / 8; ++i) {
        ref<Expr> PtrByteOfs =
          BVAddExpr::create(PtrOfs,
                            BVConstExpr::create(PtrOfs->getType().width, i));
        ref<Expr> ValByte =
          BVExtractExpr::create(Val, i*8, 8); // Assumes little endian
        StoreStmt* SS = new StoreStmt(PtrArr, PtrByteOfs, ValByte);
        addLocToStmt(SS, I);
        BBB->addStmt(SS);
      }
    } else {
      TM->NeedAdditionalByteArrayModels = true;
      std::set<GlobalArray *> Globals;
      if (PtrArr->computeArrayCandidates(Globals)) {
        std::transform(Globals.begin(), Globals.end(),
            std::inserter(TM->ModelAsByteArray, TM->ModelAsByteArray.begin()),
            [&](GlobalArray *A) { return TM->GlobalValueMap[A]; });
      } else {
        TM->NextModelAllAsByteArray = true;
      }
    }
    return;
  } else if (auto II = dyn_cast<ICmpInst>(I)) {
    ref<Expr> LHS = translateValue(II->getOperand(0)),
              RHS = translateValue(II->getOperand(1));
    E = maybeTranslateSIMDInst(BBB, II->getType(), II->getOperand(0)->getType(),
                               LHS, RHS,
                               [&](ref<Expr> LHS, ref<Expr> RHS) -> ref<Expr> {
      ref<Expr> E;
      if (II->getPredicate() == ICmpInst::ICMP_EQ)
        E = EqExpr::create(LHS, RHS);
      else if (II->getPredicate() == ICmpInst::ICMP_NE)
        E = NeExpr::create(LHS, RHS);
      else if (LHS->getType().isKind(Type::Pointer)) {
        assert(RHS->getType().isKind(Type::Pointer));
        switch (II->getPredicate()) {
        case ICmpInst::ICMP_ULT:
        case ICmpInst::ICMP_SLT: E = Expr::createPtrLt(LHS, RHS); break;
        case ICmpInst::ICMP_ULE:
        case ICmpInst::ICMP_SLE: E = Expr::createPtrLe(LHS, RHS); break;
        case ICmpInst::ICMP_UGT:
        case ICmpInst::ICMP_SGT: E = Expr::createPtrLt(RHS, LHS); break;
        case ICmpInst::ICMP_UGE:
        case ICmpInst::ICMP_SGE: E = Expr::createPtrLe(RHS, LHS); break;
        default:
          assert(0 && "Unsupported ptr icmp");
        }
      } else {
        assert(RHS->getType().isKind(Type::BV));
        switch (II->getPredicate()) {
        case ICmpInst::ICMP_UGT: E = BVUgtExpr::create(LHS, RHS); break;
        case ICmpInst::ICMP_UGE: E = BVUgeExpr::create(LHS, RHS); break;
        case ICmpInst::ICMP_ULT: E = BVUltExpr::create(LHS, RHS); break;
        case ICmpInst::ICMP_ULE: E = BVUleExpr::create(LHS, RHS); break;
        case ICmpInst::ICMP_SGT: E = BVSgtExpr::create(LHS, RHS); break;
        case ICmpInst::ICMP_SGE: E = BVSgeExpr::create(LHS, RHS); break;
        case ICmpInst::ICMP_SLT: E = BVSltExpr::create(LHS, RHS); break;
        case ICmpInst::ICMP_SLE: E = BVSleExpr::create(LHS, RHS); break;
        default:
          assert(0 && "Unsupported icmp");
        }
      }
      BBB->addStmt(new EvalStmt(E));
      return BoolToBVExpr::create(E);
    });
  } else if (auto FI = dyn_cast<FCmpInst>(I)) {
    ref<Expr> LHS = translateValue(FI->getOperand(0)),
              RHS = translateValue(FI->getOperand(1));
    E = maybeTranslateSIMDInst(BBB, FI->getType(), FI->getOperand(0)->getType(),
                               LHS, RHS,
                               [&](ref<Expr> LHS, ref<Expr> RHS) -> ref<Expr> {
      ref<Expr> E = BoolConstExpr::create(false);
      if (FI->getPredicate() & FCmpInst::FCMP_OEQ)
        E = OrExpr::create(E, FEqExpr::create(LHS, RHS));
      if (FI->getPredicate() & FCmpInst::FCMP_OGT)
        E = OrExpr::create(E, FLtExpr::create(RHS, LHS));
      if (FI->getPredicate() & FCmpInst::FCMP_OLT)
        E = OrExpr::create(E, FLtExpr::create(LHS, RHS));
      if (FI->getPredicate() & FCmpInst::FCMP_UNO)
        E = OrExpr::create(E, FUnoExpr::create(LHS, RHS));
      BBB->addStmt(new EvalStmt(E));
      return BoolToBVExpr::create(E);
    });
  } else if (auto ZEI = dyn_cast<ZExtInst>(I)) {
    ref<Expr> Op = translateValue(ZEI->getOperand(0));
    E = maybeTranslateSIMDInst(BBB, ZEI->getType(),
                               ZEI->getOperand(0)->getType(), Op,
                               [&](llvm::Type *Ty, ref<Expr> Op) {
      return BVZExtExpr::create(cast<IntegerType>(Ty)->getBitWidth(), Op);
    });
  } else if (auto SEI = dyn_cast<SExtInst>(I)) {
    ref<Expr> Op = translateValue(SEI->getOperand(0));
    E = maybeTranslateSIMDInst(BBB, SEI->getType(),
                               SEI->getOperand(0)->getType(), Op,
                               [&](llvm::Type *Ty, ref<Expr> Op) {
      return BVSExtExpr::create(cast<IntegerType>(Ty)->getBitWidth(), Op);
    });
  } else if (auto FPSII = dyn_cast<FPToSIInst>(I)) {
    ref<Expr> Op = translateValue(FPSII->getOperand(0));
    E = maybeTranslateSIMDInst(BBB, FPSII->getType(),
                               FPSII->getOperand(0)->getType(), Op,
                               [&](llvm::Type *Ty, ref<Expr> Op) {
      return FPToSIExpr::create(cast<IntegerType>(Ty)->getBitWidth(), Op);
    });
  } else if (auto FPUII = dyn_cast<FPToUIInst>(I)) {
    ref<Expr> Op = translateValue(FPUII->getOperand(0));
    E = maybeTranslateSIMDInst(BBB, FPUII->getType(),
                               FPUII->getOperand(0)->getType(), Op,
                               [&](llvm::Type *Ty, ref<Expr> Op) {
      return FPToUIExpr::create(cast<IntegerType>(Ty)->getBitWidth(), Op);
    });
  } else if (auto SIFPI = dyn_cast<SIToFPInst>(I)) {
    ref<Expr> Op = translateValue(SIFPI->getOperand(0));
    E = maybeTranslateSIMDInst(BBB, SIFPI->getType(),
                               SIFPI->getOperand(0)->getType(), Op,
                               [&](llvm::Type *Ty, ref<Expr> Op) {
      return SIToFPExpr::create(TM->TD.getTypeSizeInBits(Ty), Op);
    });
  } else if (auto UIFPI = dyn_cast<UIToFPInst>(I)) {
    ref<Expr> Op = translateValue(UIFPI->getOperand(0));
    E = maybeTranslateSIMDInst(BBB, UIFPI->getType(),
                               UIFPI->getOperand(0)->getType(), Op,
                               [&](llvm::Type *Ty, ref<Expr> Op) {
      return UIToFPExpr::create(TM->TD.getTypeSizeInBits(Ty), Op);
    });
  } else if (isa<FPExtInst>(I) || isa<FPTruncInst>(I)) {
    auto CI = cast<CastInst>(I);
    ref<Expr> Op = translateValue(CI->getOperand(0));
    E = maybeTranslateSIMDInst(BBB, CI->getType(),
                               CI->getOperand(0)->getType(), Op,
                               [&](llvm::Type *Ty, ref<Expr> Op) {
      return FPConvExpr::create(TM->TD.getTypeSizeInBits(Ty), Op);
    });
  } else if (auto TI = dyn_cast<TruncInst>(I)) {
    ref<Expr> Op = translateValue(TI->getOperand(0));
    E = maybeTranslateSIMDInst(BBB, TI->getType(),
                               TI->getOperand(0)->getType(), Op,
                               [&](llvm::Type *Ty, ref<Expr> Op) {
      return BVExtractExpr::create(Op, 0, cast<IntegerType>(Ty)->getBitWidth());
    });
  } else if (auto I2PI = dyn_cast<IntToPtrInst>(I)) {
    ref<Expr> Op = translateValue(I2PI->getOperand(0));
    E = BVToPtrExpr::create(Op);
  } else if (auto P2II = dyn_cast<PtrToIntInst>(I)) {
    ref<Expr> Op = translateValue(P2II->getOperand(0));
    E = PtrToBVExpr::create(Op);
  } else if (auto BCI = dyn_cast<BitCastInst>(I)) {
    ref<Expr> Op = translateValue(BCI->getOperand(0));
    E = TM->translateBitCast(BCI->getSrcTy(), BCI->getDestTy(), Op);
    if (Op.get() == E.get()) {
      ValueExprMap[I] = Op;
      return;
    }
  } else if (auto SI = dyn_cast<SelectInst>(I)) {
    ref<Expr> Cond = translateValue(SI->getCondition()),
              TrueVal = translateValue(SI->getTrueValue()),
              FalseVal = translateValue(SI->getFalseValue());
    Cond = BVToBoolExpr::create(Cond);
    E = IfThenElseExpr::create(Cond, TrueVal, FalseVal);
  } else if (auto EEI = dyn_cast<ExtractElementInst>(I)) {
    ref<Expr> Vec = translateValue(EEI->getVectorOperand()),
              Idx = translateValue(EEI->getIndexOperand());
    unsigned EltBits = TM->TD.getTypeSizeInBits(EEI->getType());
    BVConstExpr *CEIdx = cast<BVConstExpr>(Idx);
    unsigned UIdx = CEIdx->getValue().getZExtValue();
    E = BVExtractExpr::create(Vec, EltBits*UIdx, EltBits);
    if (EEI->getType()->isFloatingPointTy())
      E = BVToFloatExpr::create(E);
  } else if (auto IEI = dyn_cast<InsertElementInst>(I)) {
    ref<Expr> Vec = translateValue(IEI->getOperand(0)),
              NewElt = translateValue(IEI->getOperand(1)),
              Idx = translateValue(IEI->getOperand(2));
    llvm::Type *EltType = IEI->getType()->getElementType();
    if (EltType->isFloatingPointTy())
      NewElt = FloatToBVExpr::create(NewElt);
    unsigned EltBits = TM->TD.getTypeSizeInBits(EltType);
    unsigned ElemCount = IEI->getType()->getNumElements();
    BVConstExpr *CEIdx = cast<BVConstExpr>(Idx);
    unsigned UIdx = CEIdx->getValue().getZExtValue();
    std::vector<ref<Expr>> Elems;
    for (unsigned i = 0; i != ElemCount; ++i) {
      Elems.push_back(i == UIdx ? NewElt
                              : BVExtractExpr::create(Vec, EltBits*i, EltBits));
    }
    E = Expr::createBVConcatN(Elems);
  } else if (auto SVI = dyn_cast<ShuffleVectorInst>(I)) {
    ref<Expr> Vec1 = translateValue(SVI->getOperand(0)),
              Vec2 = translateValue(SVI->getOperand(1));
    unsigned EltBits =
      TM->TD.getTypeSizeInBits(SVI->getType()->getElementType());
    unsigned VecElemCount = cast<VectorType>(SVI->getOperand(0)->getType())
                              ->getNumElements();
    unsigned ResElemCount = SVI->getType()->getNumElements();
    std::vector<ref<Expr>> Elems;
    for (unsigned i = 0; i != ResElemCount; ++i) {
      ref<Expr> L;
      int MaskValI = SVI->getMaskValue(i);
      if (MaskValI < 0)
        L = BVConstExpr::create(EltBits, 0);
      else {
        unsigned MaskVal = (unsigned) MaskValI;
        if (MaskVal < VecElemCount)
          L = BVExtractExpr::create(Vec1, EltBits*MaskVal, EltBits);
        else
          L = BVExtractExpr::create(Vec2, EltBits*(MaskVal-VecElemCount),
                                    EltBits);
      }
      Elems.push_back(L);
    }
    E = Expr::createBVConcatN(Elems);
  } else if (auto CI = dyn_cast<CallInst>(I)) {
    auto F = CI->getCalledFunction();
    assert(F && "Only direct calls for now");

    CallSite CS(CI);
    std::vector<ref<Expr>> Args;
    std::transform(CS.arg_begin(), CS.arg_end(), std::back_inserter(Args),
                   [&](Value *V) { return translateValue(V); });

    if (auto II = dyn_cast<IntrinsicInst>(CI)) {
      auto ID = II->getIntrinsicID();
      auto SFII = SpecialFunctionMap.Intrinsics.find(ID);
      if (SFII != SpecialFunctionMap.Intrinsics.end()) {
        E = (this->*SFII->second)(BBB, CI, Args);
        assert(E.isNull() == CI->getType()->isVoidTy());
        if (E.isNull())
          return;
      } else {
        assert(CI->getType()->isVoidTy() && "Intrinsic unsupported, can't no-op");
        llvm::errs() << "Warning: intrinsic " << Intrinsic::getName(ID)
                     << " not supported, treating as no-op\n";
        return;
      }
    } else {
      auto SFI = SpecialFunctionMap.Functions.find(F->getName());
      if (SFI != SpecialFunctionMap.Functions.end()) {
        E = (this->*SFI->second)(BBB, CI, Args);
        assert(E.isNull() == CI->getType()->isVoidTy());
        if (E.isNull())
          return;
      } else {
        std::transform(Args.begin(), Args.end(), F->arg_begin(), Args.begin(),
                       [&](ref<Expr> E, Argument &Arg) {
                         return TM->modelValue(&Arg, E);
                       });

        auto FI = TM->FunctionMap.find(F);
        assert(FI != TM->FunctionMap.end() && "Couldn't find function in map!");
        if (CI->getType()->isVoidTy()) {
          auto CS = new CallStmt(FI->second, Args);
          addLocToStmt(CS, CI);
          BBB->addStmt(CS);
          TM->CallSites[F].push_back(&CS->getArgs());
          return;
        } else {
          E = CallExpr::create(FI->second, Args);
          EvalStmt* ES = new EvalStmt(E);
          addLocToStmt(ES, CI);
          BBB->addStmt(ES);
          ValueExprMap[I] = TM->unmodelValue(F, E);
          if (auto CE = dyn_cast<CallExpr>(E))
            TM->CallSites[F].push_back(&CE->getArgs());
          return;
        }
      }
    }
  } else if (auto RI = dyn_cast<ReturnInst>(I)) {
    if (auto V = RI->getReturnValue()) {
      assert(ReturnVar && "Returning value without return variable?");
      ref<Expr> Val = TM->modelValue(F, translateValue(V));
      BBB->addStmt(new VarAssignStmt(ReturnVar, Val));
      ReturnVals.push_back(Val);
    }
    BBB->addStmt(new ReturnStmt);
    return;
  } else if (auto BI = dyn_cast<BranchInst>(I)) {
    if (BI->isConditional()) {
      ref<Expr> Cond = BVToBoolExpr::create(translateValue(BI->getCondition()));

      bugle::BasicBlock *TrueBB = BF->addBasicBlock("truebb");
      TrueBB->addStmt(new AssumeStmt(Cond, /*partition=*/true));
      addPhiAssigns(TrueBB, I->getParent(), BI->getSuccessor(0));
      TrueBB->addStmt(new GotoStmt(BasicBlockMap[BI->getSuccessor(0)]));

      bugle::BasicBlock *FalseBB = BF->addBasicBlock("falsebb");
      FalseBB->addStmt(new AssumeStmt(NotExpr::create(Cond),
                                      /*partition=*/true));
      addPhiAssigns(FalseBB, I->getParent(), BI->getSuccessor(1));
      FalseBB->addStmt(new GotoStmt(BasicBlockMap[BI->getSuccessor(1)]));

      std::vector<bugle::BasicBlock *> BBs;
      BBs.push_back(TrueBB);
      BBs.push_back(FalseBB);
      BBB->addStmt(new GotoStmt(BBs));
    } else {
      addPhiAssigns(BBB, I->getParent(), BI->getSuccessor(0));
      BBB->addStmt(new GotoStmt(BasicBlockMap[BI->getSuccessor(0)]));
    }
    return;
  } else if (auto SI = dyn_cast<SwitchInst>(I)) {
    ref<Expr> Cond = translateValue(SI->getCondition());
    ref<Expr> DefaultExpr = BoolConstExpr::create(true);
    std::vector<bugle::BasicBlock *> Succs;

    for (auto i = SI->case_begin(), e = SI->case_end(); i != e; ++i) {
      ref<Expr> Val = TM->translateConstant(i.getCaseValue());
      bugle::BasicBlock *BB = BF->addBasicBlock("casebb");
      Succs.push_back(BB);
      BB->addStmt(new AssumeStmt(EqExpr::create(Cond, Val),/*partition=*/true));
      addPhiAssigns(BB, SI->getParent(), i.getCaseSuccessor());
      BB->addStmt(new GotoStmt(BasicBlockMap[i.getCaseSuccessor()]));
      DefaultExpr = AndExpr::create(DefaultExpr, NeExpr::create(Cond, Val));
    }

    bugle::BasicBlock *DefaultBB = BF->addBasicBlock("defaultbb");
    Succs.push_back(DefaultBB);
    DefaultBB->addStmt(new AssumeStmt(DefaultExpr, /*partition=*/true));
    addPhiAssigns(DefaultBB, SI->getParent(),
                  SI->case_default().getCaseSuccessor());
    DefaultBB->addStmt(
      new GotoStmt(BasicBlockMap[SI->case_default().getCaseSuccessor()]));

    BBB->addStmt(new GotoStmt(Succs));
    return;
  } else if (auto PN = dyn_cast<PHINode>(I)) {
    ValueExprMap[I] =
      TM->unmodelValue(PN, VarRefExpr::create(getPhiVariable(PN)));
    return;
  } else {
    assert(0 && "Unsupported instruction");
  }
  if (DumpTranslatedExprs) {
    I->dump();
    E->dump();
  }
  ValueExprMap[I] = E;
  EvalStmt* ES = new EvalStmt(E);
  addLocToStmt(ES, I);
  BBB->addStmt(ES);
  return;
}

void TranslateFunction::translateBasicBlock(bugle::BasicBlock *BBB,
                                            llvm::BasicBlock *BB) {
  for (auto i = BB->begin(), e = BB->end(); i != e; ++i)
    translateInstruction(BBB, &*i);
}
