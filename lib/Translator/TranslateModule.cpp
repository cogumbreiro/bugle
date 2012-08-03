#include "bugle/Translator/TranslateModule.h"
#include "bugle/Translator/TranslateFunction.h"
#include "bugle/Expr.h"
#include "bugle/Function.h"
#include "bugle/Module.h"
#include "bugle/Stmt.h"
#include "llvm/Constant.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Module.h"

using namespace llvm;
using namespace bugle;

ref<Expr> TranslateModule::translateConstant(Constant *C) {
  ref<Expr> &E = ConstantMap[C];
  if (E.isNull())
    E = doTranslateConstant(C);
  E->isDerivedFromConstant = true;
  return E;
}

void TranslateModule::translateGlobalInit(GlobalArray *GA, unsigned Offset,
                                          Constant *Init) {
  if (auto CS = dyn_cast<ConstantStruct>(Init)) {
    auto SL = TD.getStructLayout(CS->getType());
    for (unsigned i = 0; i < CS->getNumOperands(); ++i)
      translateGlobalInit(GA, Offset + SL->getElementOffset(i),
                          CS->getOperand(i));
  } else if (auto CA = dyn_cast<ConstantArray>(Init)) {
    uint64_t ElemSize = TD.getTypeStoreSize(CA->getType()->getElementType());
    for (unsigned i = 0; i < CA->getNumOperands(); ++i)
      translateGlobalInit(GA, Offset + i*ElemSize, CA->getOperand(i));
  } else {
    ref<Expr> Const = translateConstant(Init);
    unsigned InitByteWidth = Const->getType().width/8;
    if (GA->getRangeType() == Const->getType() && Offset % InitByteWidth == 0) {
      BM->addGlobalInit(GA, Offset/InitByteWidth, Const);
    } else if (GA->getRangeType() == Type(Type::BV, 8)) {
      if (Init->getType()->isFloatingPointTy())
        Const = FloatToBVExpr::create(Const);
      if (Init->getType()->isPointerTy())
        Const = PtrToBVExpr::create(Const);

      for (unsigned i = 0; i < Const->getType().width/8; ++i)
        BM->addGlobalInit(GA, Offset+i, BVExtractExpr::create(Const, i*8, 8));
    } else {
      NeedAdditionalByteArrayModels = true;
      ModelAsByteArray.insert(GlobalValueMap[GA]);
    }
  }
}

void TranslateModule::addGlobalArrayAttribs(GlobalArray *GA, PointerType *PT) {
  if (SL == SL_OpenCL) {
    switch (PT->getAddressSpace()) {
      case 1: GA->addAttribute("global");       break;
      case 3: GA->addAttribute("group_shared"); break;
      default: ;
    }
  }
}

GlobalArray *TranslateModule::translateGlobalVariable(GlobalVariable *GV) {
  GlobalArray *GA = getGlobalArray(GV);
  if (GV->hasInitializer() &&
      // OpenCL __local variables have bogus initialisers.
      !(SL == SL_OpenCL && GV->getType()->getAddressSpace() == 3))
    translateGlobalInit(GA, 0, GV->getInitializer());
  return GA;
}

ref<Expr> TranslateModule::translateUndef(bugle::Type t) {
  ref<Expr> E = BVConstExpr::createZero(t.width);
  if (t.isKind(Type::Float))
    return BVToFloatExpr::create(E);
  else if (t.isKind(Type::Pointer))
    return BVToPtrExpr::create(E);
  else
    return E;
}

ref<Expr> TranslateModule::doTranslateConstant(Constant *C) {
  if (auto CI = dyn_cast<ConstantInt>(C))
    return BVConstExpr::create(CI->getValue());
  if (auto CF = dyn_cast<ConstantFP>(C))
    return BVToFloatExpr::create(
             BVConstExpr::create(CF->getValueAPF().bitcastToAPInt()));
  if (auto CE = dyn_cast<ConstantExpr>(C)) {
    switch (CE->getOpcode()) {
    case Instruction::GetElementPtr: {
      ref<Expr> Op = translateConstant(CE->getOperand(0));
      return translateGEP(Op, klee::gep_type_begin(CE), klee::gep_type_end(CE),
                          [&](Value *V) {
                            return translateConstant(cast<Constant>(V));
                          });
    }
    case Instruction::BitCast:
      return translateBitCast(CE->getOperand(0)->getType(), CE->getType(),
                              translateConstant(CE->getOperand(0)));
    default:
      assert(0 && "Unhandled ConstantExpr");
    }
  }
  if (auto GV = dyn_cast<GlobalVariable>(C)) {
    GlobalArray *GA = translateGlobalVariable(GV);
    return PointerExpr::create(GlobalArrayRefExpr::create(GA),
                            BVConstExpr::createZero(TD.getPointerSizeInBits()));
  }
  if (auto UV = dyn_cast<UndefValue>(C)) {
    return translateUndef(translateType(UV->getType()));
  }
  if (auto CDS = dyn_cast<ConstantDataSequential>(C)) {
    std::vector<ref<Expr>> Elems;
    for (unsigned i = 0; i != CDS->getNumElements(); ++i) {
      if (CDS->getElementType()->isFloatingPointTy())
        Elems.push_back(
          BVConstExpr::create(CDS->getElementAsAPFloat(i).bitcastToAPInt()));
      else
        Elems.push_back(BVConstExpr::create(CDS->getElementByteSize()*8,
                                            CDS->getElementAsInteger(i)));
    }
    return Expr::createBVConcatN(Elems);
  }
  if (auto CV = dyn_cast<ConstantVector>(C)) {
    std::vector<ref<Expr>> Elems;
    std::transform(CV->op_begin(), CV->op_end(), std::back_inserter(Elems),
                   [&](Use &U) -> ref<Expr> {
      ref<Expr> E = translateConstant(cast<Constant>(U.get()));
      if (U.get()->getType()->isFloatingPointTy())
        E = FloatToBVExpr::create(E);
      return E;
    });
    return Expr::createBVConcatN(Elems);
  }
  if (auto CAZ = dyn_cast<ConstantAggregateZero>(C)) {
    ref<Expr> CE = BVConstExpr::createZero(TD.getTypeSizeInBits(CAZ->getType()));
    if (CAZ->getType()->isFloatingPointTy())
      CE = BVToFloatExpr::create(CE);
    return CE;
  }
  if (isa<ConstantPointerNull>(C)) {
    return PointerExpr::create(NullArrayRefExpr::create(), 
                            BVConstExpr::createZero(TD.getPointerSizeInBits()));
  }
  assert(0 && "Unhandled constant");
  return 0;
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

bugle::Type TranslateModule::translateArrayRangeType(llvm::Type *T) {
  if (auto AT = dyn_cast<ArrayType>(T))
    return translateArrayRangeType(AT->getElementType());
  if (auto VT = dyn_cast<VectorType>(T))
    return translateArrayRangeType(VT->getElementType());
  if (isa<StructType>(T))
    return Type(Type::BV, 8);

  return translateType(T);
}

bugle::GlobalArray *TranslateModule::getGlobalArray(llvm::Value *V) {
  GlobalArray *&GA = ValueGlobalMap[V];
  if (GA)
    return GA;

  bugle::Type T(Type::BV, 8);
  auto PT = cast<PointerType>(V->getType());
  if (!(ModelAllAsByteArray ||
        ModelAsByteArray.find(V) != ModelAsByteArray.end()))
    T = translateArrayRangeType(PT->getElementType());
  GA = BM->addGlobal(V->getName(), T);
  addGlobalArrayAttribs(GA, PT);
  GlobalValueMap[GA] = V;
  return GA;
}

ref<Expr> TranslateModule::translateGEP(ref<Expr> Ptr,
                                        klee::gep_type_iterator begin,
                                        klee::gep_type_iterator end,
                                      std::function<ref<Expr>(Value *)> xlate) {
  ref<Expr> PtrArr = ArrayIdExpr::create(Ptr, defaultRange()),
            PtrOfs = ArrayOffsetExpr::create(Ptr);
  for (auto i = begin; i != end; ++i) {
    if (StructType *st = dyn_cast<StructType>(*i)) {
      const StructLayout *sl = TD.getStructLayout(st);
      const ConstantInt *ci = cast<ConstantInt>(i.getOperand());
      uint64_t addend = sl->getElementOffset((unsigned) ci->getZExtValue());
      PtrOfs = BVAddExpr::create(PtrOfs,
                 BVConstExpr::create(BM->getPointerWidth(), addend));
    } else {
      const SequentialType *set = cast<SequentialType>(*i);
      uint64_t elementSize = 
        TD.getTypeStoreSize(set->getElementType());
      Value *operand = i.getOperand();
      ref<Expr> index = xlate(operand);
      index = BVSExtExpr::create(BM->getPointerWidth(), index);
      ref<Expr> addend =
        BVMulExpr::create(index,
          BVConstExpr::create(BM->getPointerWidth(), elementSize));
      PtrOfs = BVAddExpr::create(PtrOfs, addend);
    }
  }
  return PointerExpr::create(PtrArr, PtrOfs);
}

ref<Expr> TranslateModule::translateBitCast(llvm::Type *SrcTy,
                                            llvm::Type *DestTy,
                                            ref<Expr> Op) {
  if (SrcTy->isFloatingPointTy() && !DestTy->isFloatingPointTy()) {
    return FloatToBVExpr::create(Op);
  } else if (!SrcTy->isFloatingPointTy() && DestTy->isFloatingPointTy()) {
    return BVToFloatExpr::create(Op);
  } else {
    return Op;
  }
}

void TranslateModule::addGPUEntryPoint(StringRef Name) {
  GPUEntryPoints.insert(Name);
}

static bool isAxiomFunction(StringRef Name) {
  return Name.startswith("__axiom");
}

// Convert the given unmodelled expression E to modelled form.
ref<Expr> TranslateModule::modelValue(Value *V, ref<Expr> E) {
  if (E->getType().isKind(Type::Pointer)) {
    auto OI = ModelPtrAsGlobalOffset.find(V);
    if (OI != ModelPtrAsGlobalOffset.end()) {
      auto GA = getGlobalArray(*OI->second.begin());
      auto Ofs = ArrayOffsetExpr::create(E);
      Ofs = Expr::createExactBVUDiv(Ofs, GA->getRangeType().width/8);
      assert(!Ofs.isNull() && "Couldn't create div this time!");

      if (OI->second.size() == 1) {
        return Ofs;
      } else {
        return PointerExpr::create(ArrayIdExpr::create(E, defaultRange()), Ofs);
      }
    }
  }

  return E;
}

// If the given value is modelled, return its modelled type, else return
// its conventional Boogie type (translateType).
bugle::Type TranslateModule::getModelledType(Value *V) {
  auto OI = ModelPtrAsGlobalOffset.find(V);
  if (OI != ModelPtrAsGlobalOffset.end() && OI->second.size() == 1) {
    return Type(Type::BV, TD.getPointerSizeInBits());
  } else {
    return translateType(V->getType());
  }
}

// Convert the given modelled expression E to unmodelled form.
ref<Expr> TranslateModule::unmodelValue(Value *V, ref<Expr> E) {
  auto OI = ModelPtrAsGlobalOffset.find(V);
  if (OI != ModelPtrAsGlobalOffset.end()) {
    auto GA = getGlobalArray(*OI->second.begin());
    auto WidthCst = BVConstExpr::create(TD.getPointerSizeInBits(),
                                        GA->getRangeType().width/8);
    if (OI->second.size() == 1) {
      return PointerExpr::create(GlobalArrayRefExpr::create(GA),
                                 BVMulExpr::create(E, WidthCst));
    } else {
      std::set<GlobalArray *> Globals;
      std::transform(OI->second.begin(), OI->second.end(),
                     std::inserter(Globals, Globals.begin()),
                     [&](Value *V) { return getGlobalArray(V); });

      return PointerExpr::create(MemberOfExpr::create(ArrayIdExpr::create(E,
                                                                defaultRange()),
                                                      Globals),
                                 BVMulExpr::create(ArrayOffsetExpr::create(E),
                                                   WidthCst));
    }
  } else {
    return E;
  }
}

/// Given a value and all possible Boogie expressions to which it may be
/// assigned, compute a model for that value such that future invocations
/// of modelValue/getModelledType/unmodelValue use that model.
void TranslateModule::computeValueModel(Value *Val, Var *Var,
                                        llvm::ArrayRef<ref<Expr>> Assigns) {
  if (!Val->getType()->isPointerTy())
    return;
  if (ModelPtrAsGlobalOffset.find(Val) != ModelPtrAsGlobalOffset.end())
    return;

  std::set<GlobalArray *> GlobalSet;
  for (auto ai = Assigns.begin(), ae = Assigns.end(); ai != ae; ++ai) {
    if ((*ai)->computeArrayCandidates(GlobalSet)) {
      continue;
    } else {
      // See if this assignment is simply referring back to the variable
      // itself.
      if (auto VRE = dyn_cast<VarRefExpr>(*ai)) {
        if (VRE->getVar() == Var)
          continue;
      }
      if (auto PE = dyn_cast<PointerExpr>(*ai)) {
        if (auto AIE = dyn_cast<ArrayIdExpr>(PE->getArray())) {
          if (auto VRE = dyn_cast<VarRefExpr>(AIE->getSubExpr())) {
            if (VRE->getVar() == Var)
              continue;
          }
        }
      }
    }

    return;
  }

  assert(!GlobalSet.empty() && "GlobalSet is empty?");

  // Now check that each array in GlobalSet has the same type.
  Type GlobalsType = Expr::getArrayCandidateType(GlobalSet);

  // Check that each offset is a multiple of the range type's byte width (or
  // that if the offset refers to the variable, it maintains the invariant).
  bool ModelGlobalsAsByteArray = false;
  if (GlobalsType.isKind(Type::Any) || GlobalsType.isKind(Type::Unknown)) {
    ModelGlobalsAsByteArray = true;
  } else {
    for (auto ai = Assigns.begin(), ae = Assigns.end(); ai != ae; ++ai) {
      auto AOE = ArrayOffsetExpr::create(*ai);
      if (Expr::createExactBVUDiv(AOE, GlobalsType.width/8, Var).isNull()) {
        ModelGlobalsAsByteArray = true;
        break;
      }
    }
  }

  // Success!  Record the global set.
  auto i = GlobalSet.find(0);
  if (i != GlobalSet.end())
    GlobalSet.erase(i);

  auto &GlobalValSet = ModelPtrAsGlobalOffset[Val];
  std::transform(GlobalSet.begin(), GlobalSet.end(),
                 std::inserter(GlobalValSet, GlobalValSet.begin()), 
                 [&](GlobalArray *A) { return GlobalValueMap[A]; });
  NeedAdditionalGlobalOffsetModels = true;

  if (ModelGlobalsAsByteArray) {
    std::transform(GlobalSet.begin(), GlobalSet.end(),
                   std::inserter(ModelAsByteArray, ModelAsByteArray.begin()), 
                   [&](GlobalArray *A) { return GlobalValueMap[A]; });
    NeedAdditionalByteArrayModels = true;
  }
}

void TranslateModule::translate() {
  do {
    NeedAdditionalByteArrayModels = false;
    NeedAdditionalGlobalOffsetModels = false;

    delete BM;
    BM = new bugle::Module;

    FunctionMap.clear();
    ConstantMap.clear();
    GlobalValueMap.clear();
    ValueGlobalMap.clear();
    CallSites.clear();

    BM->setPointerWidth(TD.getPointerSizeInBits());

    for (auto i = M->begin(), e = M->end(); i != e; ++i) {
      if (i->isIntrinsic() || isAxiomFunction(i->getName()) ||
          TranslateFunction::isSpecialFunction(SL, i->getName()))
        continue;

      auto BF = FunctionMap[&*i] = BM->addFunction(i->getName());

      auto RT = i->getFunctionType()->getReturnType();
      if (!RT->isVoidTy())
        BF->addReturn(translateType(RT), "ret");
    }

    for (auto i = M->begin(), e = M->end(); i != e; ++i) {
      if (i->isIntrinsic())
        continue;

      if (isAxiomFunction(i->getName())) {
        bugle::Function F("");
        Type RT = translateType(i->getFunctionType()->getReturnType());
        Var *RV = F.addReturn(RT, "ret");
        TranslateFunction TF(this, &F, &*i);
        TF.translate();
        assert(F.begin()+1 == F.end() && "Expected one basic block");
        bugle::BasicBlock *BB = *F.begin();
        VarAssignStmt *S = cast<VarAssignStmt>(*(BB->end()-2));
        assert(S->getVars()[0] == RV);
        BM->addAxiom(Expr::createNeZero(S->getValues()[0]));
      } else if (!TranslateFunction::isSpecialFunction(SL, i->getName())) {
        TranslateFunction TF(this, FunctionMap[&*i], &*i);
        TF.isGPUEntryPoint =
          (GPUEntryPoints.find(i->getName()) != GPUEntryPoints.end());
        TF.translate();
      }
    }

    // If this round gave us a case split, examine each pointer argument to
    // each call site for each function to see if the argument always refers to
    // the same global array, in which case we can model the parameter as an
    // offset, and potentially avoid the case split.
    if (!ModelAllAsByteArray && NextModelAllAsByteArray) {
      for (auto i = CallSites.begin(), e = CallSites.end(); i != e; ++i) {
             unsigned pidx = 0;
        for (auto pi = i->first->arg_begin(), pe = i->first->arg_end();
             pi != pe; ++pi, ++pidx) {
          std::vector<ref<Expr>> Parms;
          std::transform(i->second.begin(), i->second.end(),
                         std::back_inserter(Parms),
                         [&](const std::vector<ref<Expr>> *cs) {
                           return (*cs)[pidx];
                         });
          computeValueModel(pi, 0, Parms);
        }
      }
    }

    if (NeedAdditionalGlobalOffsetModels) {
      // If we can model new pointers using global offsets, a previously
      // observed case split may become unnecessary.  So when we recompute the
      // fixed point, don't use byte array models for everything unless we're
      // stuck.
      ModelAllAsByteArray = NextModelAllAsByteArray = false;
    } else {
      ModelAllAsByteArray = NextModelAllAsByteArray;
    }
  } while (NeedAdditionalByteArrayModels || NeedAdditionalGlobalOffsetModels);
}
