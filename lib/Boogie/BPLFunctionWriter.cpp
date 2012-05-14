#include "bugle/BPLFunctionWriter.h"
#include "bugle/BPLModuleWriter.h"
#include "llvm/Support/raw_ostream.h"
#include "bugle/BasicBlock.h"
#include "bugle/Casting.h"
#include "bugle/Expr.h"
#include "bugle/Function.h"
#include "bugle/GlobalArray.h"
#include "bugle/Stmt.h"

using namespace bugle;

namespace {

struct ScopedParenPrinter {
  llvm::raw_ostream &OS;
  bool ParenRequired;
  ScopedParenPrinter(llvm::raw_ostream &OS, unsigned Depth, unsigned RuleDepth)
    : OS(OS), ParenRequired(RuleDepth < Depth) {
    if (ParenRequired)
      OS << "(";
  }
  ~ScopedParenPrinter() {
    if (ParenRequired)
      OS << ")";
  }
};

}

void BPLFunctionWriter::writeExpr(llvm::raw_ostream &OS, Expr *E,
                                  unsigned Depth = 0) {
  auto id = SSAVarIds.find(E);
  if (id != SSAVarIds.end()) {
    OS << "v" << id->second;
    return;
  }

  if (auto CE = dyn_cast<BVConstExpr>(E)) {
    auto &Val = CE->getValue();
    Val.print(OS, /*isSigned=*/false);
    OS << "bv" << Val.getBitWidth();
  } else if (auto BCE = dyn_cast<BoolConstExpr>(E)) {
    OS << (BCE->getValue() ? "true" : "false");
  } else if (auto EE = dyn_cast<BVExtractExpr>(E)) {
    ScopedParenPrinter X(OS, Depth, 8);
    writeExpr(OS, EE->getSubExpr().get(), 9);
    OS << "[" << (EE->getOffset() + EE->getType().width) << ":"
       << EE->getOffset() << "]";
  } else if (auto ZEE = dyn_cast<BVZExtExpr>(E)) {
    OS << "BV" << ZEE->getSubExpr()->getType().width
       << "_ZEXT" << ZEE->getType().width << "(";
    writeExpr(OS, ZEE->getSubExpr().get());
    OS << ")";
    MW->writeIntrinsic([&](llvm::raw_ostream &OS) {
      OS << "function {:bvbuiltin \"zero_extend\"} BV"
         << ZEE->getSubExpr()->getType().width
         << "_ZEXT" << ZEE->getType().width
         << "(bv" << ZEE->getSubExpr()->getType().width
         << ") : bv" << ZEE->getType().width;
    });
  } else if (auto SEE = dyn_cast<BVSExtExpr>(E)) {
    OS << "BV" << SEE->getSubExpr()->getType().width
       << "_SEXT" << SEE->getType().width << "(";
    writeExpr(OS, SEE->getSubExpr().get());
    OS << ")";
    MW->writeIntrinsic([&](llvm::raw_ostream &OS) {
      OS << "function {:bvbuiltin \"sign_extend\"} BV"
         << SEE->getSubExpr()->getType().width
         << "_SEXT" << SEE->getType().width
         << "(bv" << SEE->getSubExpr()->getType().width
         << ") : bv" << SEE->getType().width;
    });
  } else if (auto LE = dyn_cast<LoadExpr>(E)) {
    ref<Expr> PtrArr = LE->getArray();
    if (auto ArrE = dyn_cast<GlobalArrayRefExpr>(PtrArr)) {
      ScopedParenPrinter X(OS, Depth, 8);
      OS << ArrE->getArray()->getName() << "[";
      writeExpr(OS, LE->getOffset().get(), 9);
      OS << "]";
    } else {
      assert(0 && "TODO case split");
    }
  } else if (auto PtrE = dyn_cast<PointerExpr>(E)) {
    OS << "MKPTR(";
    writeExpr(OS, PtrE->getArray().get());
    OS << ", ";
    writeExpr(OS, PtrE->getOffset().get());
    OS << ")";
  } else if (auto VarE = dyn_cast<VarRefExpr>(E)) {
    OS << VarE->getVar()->getName();
  } else if (auto ArrE = dyn_cast<GlobalArrayRefExpr>(E)) {
    OS << "arrayId_" << ArrE->getArray()->getName();
  } else if (auto ConcatE = dyn_cast<BVConcatExpr>(E)) {
    ScopedParenPrinter X(OS, Depth, 4);
    writeExpr(OS, ConcatE->getLHS().get(), 4);
    OS << " ++ ";
    writeExpr(OS, ConcatE->getRHS().get(), 5);
  } else if (auto B2BVE = dyn_cast<BoolToBVExpr>(E)) {
    OS << "(if ";
    writeExpr(OS, B2BVE->getSubExpr().get());
    OS  << " then 1bv1 else 0bv1)";
  } else if (auto NotE = dyn_cast<NotExpr>(E)) {
    ScopedParenPrinter X(OS, Depth, 7);
    OS << "!";
    writeExpr(OS, NotE->getSubExpr().get(), 8);
  } else if (auto BinE = dyn_cast<BinaryExpr>(E)) {
    switch (BinE->getKind()) {
    case Expr::BVAdd:
      OS << "BV" << BinE->getType().width << "_ADD";
      MW->writeIntrinsic([&](llvm::raw_ostream &OS) {
        OS << "function {:bvbuiltin \"bvadd\"} BV" << BinE->getType().width
           << "_ADD(bv" << BinE->getType().width
           << ", bv" << BinE->getType().width
           << ") : bv" << BinE->getType().width;
      });
      break;
    case Expr::BVSub:
      OS << "BV" << BinE->getType().width << "_SUB";
      MW->writeIntrinsic([&](llvm::raw_ostream &OS) {
        OS << "function {:bvbuiltin \"bvsub\"} BV" << BinE->getType().width
           << "_SUB(bv" << BinE->getType().width
           << ", bv" << BinE->getType().width
           << ") : bv" << BinE->getType().width;
      });
      break;
    case Expr::BVMul:
      OS << "BV" << BinE->getType().width << "_MUL";
      MW->writeIntrinsic([&](llvm::raw_ostream &OS) {
        OS << "function {:bvbuiltin \"bvmul\"} BV" << BinE->getType().width
           << "_MUL(bv" << BinE->getType().width
           << ", bv" << BinE->getType().width
           << ") : bv" << BinE->getType().width;
      });
      break;
    case Expr::BVSDiv:
      OS << "BV" << BinE->getType().width << "_SDIV";
      MW->writeIntrinsic([&](llvm::raw_ostream &OS) {
        OS << "function {:bvbuiltin \"bvsdiv\"} BV" << BinE->getType().width
           << "_SDIV(bv" << BinE->getType().width
           << ", bv" << BinE->getType().width
           << ") : bv" << BinE->getType().width;
      });
      break;
    case Expr::BVUDiv:
      OS << "BV" << BinE->getType().width << "_UDIV";
      MW->writeIntrinsic([&](llvm::raw_ostream &OS) {
        OS << "function {:bvbuiltin \"bvudiv\"} BV" << BinE->getType().width
           << "_UDIV(bv" << BinE->getType().width
           << ", bv" << BinE->getType().width
           << ") : bv" << BinE->getType().width;
      });
      break;
    case Expr::BVSgt:
      OS << "BV" << BinE->getLHS()->getType().width << "_SGT";
      MW->writeIntrinsic([&](llvm::raw_ostream &OS) {
        OS << "function {:bvbuiltin \"bvsgt\"} BV"
           << BinE->getLHS()->getType().width
           << "_SGT(bv" << BinE->getLHS()->getType().width
           << ", bv" << BinE->getLHS()->getType().width
           << ") : bool";
      });
      break;
    default:
      assert(0 && "Unsupported binary expr");
      break;
    }
    OS << "(";
    writeExpr(OS, BinE->getLHS().get());
    OS << ", ";
    writeExpr(OS, BinE->getRHS().get());
    OS << ")";
  } else {
    assert(0 && "Unsupported expression");
  }
}

void BPLFunctionWriter::writeStmt(llvm::raw_ostream &OS, Stmt *S) {
  if (auto ES = dyn_cast<EvalStmt>(S)) {
    unsigned id = SSAVarIds.size();
    OS << "  v" << id << " := ";
    writeExpr(OS, ES->getExpr().get());
    OS << ";\n";
    SSAVarIds[ES->getExpr().get()] = id;
  } else if (auto SS = dyn_cast<StoreStmt>(S)) {
    ref<Expr> PtrArr = SS->getArray();
    if (auto ArrE = dyn_cast<GlobalArrayRefExpr>(PtrArr)) {
      ModifiesSet.insert(ArrE->getArray());
      OS << "  " << ArrE->getArray()->getName() << "[";
      writeExpr(OS, SS->getOffset().get());
      OS << "] := ";
      writeExpr(OS, SS->getValue().get());
      OS << ";\n";
    } else {
      assert(0 && "TODO case split");
    }
  } else if (auto VAS = dyn_cast<VarAssignStmt>(S)) {
    OS << "  " << VAS->getVar()->getName() << " := ";
    writeExpr(OS, VAS->getValue().get());
    OS << ";\n";
  } else if (isa<ReturnStmt>(S)) {
    OS << "  return;\n";
  } else {
    assert(0 && "Unsupported statement");
  }
}

void BPLFunctionWriter::writeBasicBlock(llvm::raw_ostream &OS, BasicBlock *BB) {
  OS << BB->getName() << ":\n";
  for (auto i = BB->begin(), e = BB->end(); i != e; ++i)
    writeStmt(OS, *i);
}

void BPLFunctionWriter::writeVar(llvm::raw_ostream &OS, Var *V) {
  OS << V->getName() << ":";
  MW->writeType(OS, V->getType());
}

void BPLFunctionWriter::write() {
  OS << "procedure " << F->getName() << "(";
  for (auto b = F->arg_begin(), i = b, e = F->arg_end(); i != e; ++i) {
    if (i != b)
      OS << ", ";
    writeVar(OS, *i);
  }
  OS << ")";

  if (F->return_begin() != F->return_end()) {
    OS << " returns (";
    for (auto b = F->return_begin(), i = b, e = F->return_end(); i != e; ++i) {
      if (i != b)
        OS << ", ";
      writeVar(OS, *i);
    }
    OS << ")";
  }

  if (F->begin() == F->end()) {
    OS << ";\n";
  } else {
    std::string Body;
    llvm::raw_string_ostream BodyOS(Body);
    std::for_each(F->begin(), F->end(),
                  [&](BasicBlock *BB){ writeBasicBlock(BodyOS, BB); });
    if (!ModifiesSet.empty()) {
      OS << " modifies ";
      for (auto b = ModifiesSet.begin(), i = b, e = ModifiesSet.end(); i != e;
           ++i) {
        if (i != b)
          OS << ", ";
        OS << (*i)->getName();
      }
      OS << ";";
    }

    OS << " {\n";

    for (auto i = SSAVarIds.begin(), e = SSAVarIds.end(); i != e; ++i) {
      OS << "  var v" << i->second << ":";
      MW->writeType(OS, i->first->getType());
      OS << ";\n";
    }

    OS << BodyOS.str();
    OS << "}\n";
  }
}
