#include "bugle/BPLModuleWriter.h"
#include "bugle/BPLFunctionWriter.h"
#include "bugle/Module.h"
#include "bugle/Type.h"
#include "llvm/Support/raw_ostream.h"

using namespace bugle;

void BPLModuleWriter::writeType(llvm::raw_ostream &OS, const Type &t) {
  switch (t.kind) {
  case Type::Bool:
    OS << "bool";
    break;
  case Type::BV:
    OS << "bv" << t.width;
    break;
  case Type::Float:
    switch (t.width) {
    case 32:
      OS << "float";
      break;
    case 64:
      OS << "double";
      break;
    default:
      assert(0 && "Unexpected float width");
    }
    break;
  case Type::Pointer:
    OS << "ptr";
    break;
  case Type::ArrayId:
    OS << "arrayId";
    break;
  }
}

void BPLModuleWriter::writeIntrinsic(std::function<void(llvm::raw_ostream&)> F){
  std::string S;
  llvm::raw_string_ostream SS(S);
  F(SS);
  IntrinsicSet.insert(SS.str());
}

void BPLModuleWriter::write() {
  std::string S;
  llvm::raw_string_ostream SS(S);
  for (auto i = M->begin(), e = M->end(); i != e; ++i) {
    BPLFunctionWriter FW(this, SS, *i);
    FW.write();
  }

  for (auto i = IntrinsicSet.begin(), e = IntrinsicSet.end(); i != e; ++i) {
    OS << *i << ";\n";
  }

  OS << SS.str();
}
