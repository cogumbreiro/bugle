#include "bugle/IntegerRepresentation.h"
#include "llvm/Support/ErrorHandling.h"
#include <sstream>

namespace bugle {

std::string MathIntegerRepresentation::getType(unsigned bitWidth) {
  return "int";
}

std::string MathIntegerRepresentation::getLiteralSuffix(unsigned bitWidth) {
  return "";
}

std::string MathIntegerRepresentation::getLiteral(unsigned literal,
                                                  unsigned bitWidth) {
  std::stringstream ss;
  ss << literal;
  return ss.str();
}

std::string MathIntegerRepresentation::getZeroExtend(unsigned FromWidth,
                                                     unsigned ToWidth) {
  std::stringstream ss;
  ss << "function {:inline true} BV" << FromWidth << "_ZEXT" << ToWidth
     << "(x : int) : int {\n"
     << "  x\n"
     << "}";
  return ss.str();
}

std::string MathIntegerRepresentation::getSignExtend(unsigned FromWidth,
                                                     unsigned ToWidth) {
  std::stringstream ss;
  ss << "function BV" << FromWidth << "_SEXT" << ToWidth << "(int) : int";
  return ss.str();
}

std::string MathIntegerRepresentation::getArithmeticBinary(std::string Name, 
    bugle::Expr::Kind Kind, unsigned Width) {
  std::stringstream ss;
  ss << "function ";

  switch (Kind) {
  case Expr::BVAdd:
  case Expr::BVSub:
  case Expr::BVMul:
  case Expr::BVUDiv:
  case Expr::BVSDiv:
  case Expr::BVURem:
  case Expr::BVSRem:
    const char *infixOp;
    switch (Kind) {
    case Expr::BVAdd:  infixOp = "+"; break;
    case Expr::BVSub:  infixOp = "-"; break;
    case Expr::BVMul:  infixOp = "*"; break;
    case Expr::BVUDiv:
    case Expr::BVSDiv: infixOp = "div"; break;
    case Expr::BVURem:
    case Expr::BVSRem: infixOp = "mod"; break;
    default: llvm_unreachable("huh?");
    }
    ss << "{:inline true} BV" << Width << "_" << Name
       << "(x : int, y : int) : int {\n"
       << "  x " << infixOp << " y\n"
       << "}";
    return ss.str();
  default:
    /* do nothing */
    break;
  }

  if (Kind == Expr::BVAnd) {
    ss << "{:inline true} BV" << Width << "_" << Name
       << "(x : int, y : int) : int {\n"
       << "  if x == y then x "
       << "else (if x == 0 || y == 0 then 0 "
       << "else BV" << Width << "_" << Name << "_UF(x, y))\n"
       << "}\n"
       << "function BV" << Width << "_" << Name << "_UF(int, int) : int;";
    return ss.str();
  }

  if (Kind == Expr::BVOr) {
    ss << "{:inline true} BV" << Width << "_" << Name
       << "(x : int, y : int) : int {\n"
       << "  if x == y then x "
       << "else (if x == 0 then y else (if y == 0 then x "
       << "else BV" << Width << "_" << Name << "_UF(x, y)))\n"
       << "}\n"
       << "function BV" << Width << "_" << Name << "_UF(int, int) : int;";
    return ss.str();
  }

  if (Kind == Expr::BVXor) {
    ss << "{:inline true} BV" << Width << "_" << Name
       << "(x : int, y : int) : int {\n";

    if (Width == 1) {
      ss << "  if (x == 1 || x == -1) && (y == 1 || y == -1) then 0 else (\n"
         << "    if (x == 1 || x == -1) && y == 0 then 1 else (\n"
         << "      if x == 0 && (y == 1 || y == -1) then 1 else (\n"
         << "        if x == y then 0 else BV" << Width << "_" << Name << "_UF(x, y))))\n";
    } else {
      ss << "  if x == y then 0 "
         << "else (if x == 0 then y else (if y == 0 then x "
         << "else BV" << Width << "_" << Name << "_UF(x, y)))\n";
    }

    ss << "}\n"
       << "function BV" << Width << "_" << Name << "_UF(int, int) : int;";
    return ss.str();
  }

  if (Kind == Expr::BVShl) {
    ss << "{:inline true} BV" << Width << "_" << Name
       << "(x : int, y : int) : int {\n"
       << "  if x >= 0 && y == 1 then x*2 else BV" << Width << "_" << Name << "_UF(x,y)\n"
       << "}\n"
       << "function BV" << Width << "_" << Name << "_UF(int, int) : int;";
    return ss.str();
  }

  switch (Kind) {
  case Expr::BVAShr:
  case Expr::BVLShr:
    ss << " BV" << Width << "_" << Name
      << "(int, int) : int;";
    return ss.str();
  default: llvm_unreachable("huh?");
  }
}

std::string MathIntegerRepresentation::getBooleanBinary(std::string Name, 
    bugle::Expr::Kind Kind, unsigned Width) {
  const char *infixOp;
  switch (Kind) {
  case Expr::BVUgt: infixOp = ">"; break;
  case Expr::BVUge: infixOp = ">="; break;
  case Expr::BVUlt: infixOp = "<"; break;
  case Expr::BVUle: infixOp = "<="; break;
  case Expr::BVSgt: infixOp = ">"; break;
  case Expr::BVSge: infixOp = ">="; break;
  case Expr::BVSlt: infixOp = "<"; break;
  case Expr::BVSle: infixOp = "<="; break;
  default: llvm_unreachable("huh?");
  }

  std::stringstream ss;
  ss << "function {:inline true} BV" << Width << "_" << Name
     << "(x : int, y : int) : bool {\n"
     << "  x " << infixOp << " y\n"
     << "}";
  return ss.str();
}

void MathIntegerRepresentation::printVal(llvm::raw_ostream &OS,
                                         const llvm::APInt &Val) {
  Val.print(OS, /*isSigned=*/true);
}

std::string MathIntegerRepresentation::getExtractExpr(const std::string &Expr,
    unsigned UpperBit, unsigned LowerBit) {
  std::stringstream ss;
  ss << "BV_EXTRACT(" << Expr << ", " << UpperBit << ", " << LowerBit << ")";
  return ss.str();
}

bool MathIntegerRepresentation::abstractsExtract() {
  return true;
}

std::string MathIntegerRepresentation::getExtract() {
  return "function BV_EXTRACT(int, int, int) : int;";
}


bool MathIntegerRepresentation::abstractsConcat() {
  return true;
}

std::string MathIntegerRepresentation::getConcat() {
  return "function BV_CONCAT(int, int) : int;";
}

std::string MathIntegerRepresentation::getConcatExpr(const std::string &Lhs,
                                                     const std::string &Rhs) {
  return "BV_CONCAT(" + Lhs + ", " + Rhs + ")";
}

}
