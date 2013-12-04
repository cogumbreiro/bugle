#include "bugle/BPLModuleWriter.h"
#include "bugle/BPLFunctionWriter.h"
#include "bugle/IntegerRepresentation.h"
#include "bugle/Module.h"
#include "bugle/Type.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace bugle;

void BPLModuleWriter::writeType(llvm::raw_ostream &OS, const Type &t) {
  if (t.array) {
    UsesPointers = true;
    OS << "arrayId";
    return;
  }
  switch (t.kind) {
  case Type::Bool:
    OS << "bool";
    break;
  case Type::BV:
    OS << MW->IntRep->getType(t.width);
    break;
  case Type::Pointer:
    UsesPointers = true;
    OS << "ptr";
    break;
  case Type::Any:
  case Type::Unknown:
    llvm_unreachable("Module writer found unexpected type");
  }
}

void BPLModuleWriter::writeIntrinsic(std::function<void(llvm::raw_ostream&)> F,
                                     bool addSeparator) {
  if (this == 0)
    return;

  std::string S;
  llvm::raw_string_ostream SS(S);
  F(SS);
  if (addSeparator) {
    SS << ";";
  }
  IntrinsicSet.insert(SS.str());
}

const std::string &BPLModuleWriter::getGlobalInitRequires() {
  if (GlobalInitRequires.empty() &&
      M->global_init_begin() != M->global_init_end()) {
    llvm::raw_string_ostream SS(GlobalInitRequires);
    SS << "requires ";
    for (auto i = M->global_init_begin(), e = M->global_init_end();
         i != e; ++i) {
      if (i != M->global_init_begin())
        SS << " &&\n         ";
      SS << "$$" << i->array->getName() << "[" 
        << MW->IntRep->getLiteral(i->offset, M->getPointerWidth()) << "] == ";
      writeExpr(SS, i->init.get());
    }
    SS << ";\n";
  }
  return GlobalInitRequires;
}

void BPLModuleWriter::write() {
  std::string S;
  llvm::raw_string_ostream SS(S);
  for (auto i = M->begin(), e = M->end(); i != e; ++i) {
    BPLFunctionWriter FW(this, SS, *i);
    FW.write();
  }
  for (auto i = M->axiom_begin(), e = M->axiom_end(); i != e; ++i) {
    SS << "axiom ";
    writeExpr(SS, i->get());
    SS << ";\n";
  }

  if (UsesPointers) {
    OS << "type {:datatype} ptr;\n"
       << "type arrayId;\n"
       << "function {:constructor} MKPTR(base: arrayId, offset: "
       << MW->IntRep->getType(M->getPointerWidth())
       << ") : ptr;\n"
       << "function PTR_LT(lhs: ptr, rhs: ptr) : bool;\n\n";
  }

  unsigned long int sizes = 0;
  for (auto i = M->global_begin(), e = M->global_end(); i != e; ++i) {
    unsigned long int size = (1 << (((*i)->getRangeType().width/8) ));
    if (!(size & sizes)) {
      auto pw = MW->IntRep->getType(M->getPointerWidth());
      auto bw = MW->IntRep->getType((*i)->getRangeType().width);
      OS << "procedure _ATOMIC_OP" << (*i)->getRangeType().width << "(x : ["
         << pw << "]" << bw << ", y : "
         << pw << ") returns (z : " << bw << ", A : ["
         << MW->IntRep->getType(32) << "]" << bw << ");\n";
      sizes = size | sizes;
    }
  }

  for (auto i = M->global_begin(), e = M->global_end(); i != e; ++i) {

    OS << "var ";
    for (auto ai = (*i)->attrib_begin(), ae = (*i)->attrib_end(); ai != ae;
         ++ai) {
      OS << "{:" << *ai << "} ";
    }
    OS << "$$" << (*i)->getName() << " : ["
       << MW->IntRep->getType(M->getPointerWidth())
       << "]";
    writeType(OS, (*i)->getRangeType());
    OS << ";\n";

    if ((*i)->isGlobalOrGroupShared()) {
      std::string prefix = "var {:race_checking} ";
      if ((*i)->isGlobal())
        prefix += "{:global} ";
      else if ((*i)->isGroupShared())
        prefix += "{:group_shared} ";

      OS << prefix << "{:elem_width " << (*i)->getRangeType().width
         << "} " << "_READ_HAS_OCCURRED_$$"
         << (*i)->getName() << " : bool;\n";
      OS << prefix << "{:elem_width " << (*i)->getRangeType().width
         << "} " << "_WRITE_HAS_OCCURRED_$$"
         << (*i)->getName() << " : bool;\n";
      OS << prefix << "{:elem_width " << (*i)->getRangeType().width
         << "} " << "_ATOMIC_HAS_OCCURRED_$$"
         << (*i)->getName() << " : bool;\n";
      OS << prefix << "_READ_OFFSET_$$" << (*i)->getName() << " : "
         << MW->IntRep->getType(32) << ";\n";
      OS << prefix << "_WRITE_OFFSET_$$" << (*i)->getName() << " : "
         << MW->IntRep->getType(32) << ";\n";
      OS << prefix << "_ATOMIC_OFFSET_$$" << (*i)->getName() << " : "
         << MW->IntRep->getType(32) << ";\n";

      if ((*i)->getNotAccessedExpr()) {
        OS << "var {:check_access} _NOT_ACCESSED_$$" << (*i)->getName() << " : "
           << MW->IntRep->getType(M->getPointerWidth()) << ";\n";
      }
    }

    if (UsesPointers)
      OS << "const unique $arrayId$$" << (*i)->getName() << " : arrayId;\n";

    OS << "\n";
  }

  if (UsesPointers)
    OS << "const unique $arrayId$$null : arrayId;\n\n";

  for (auto i = IntrinsicSet.begin(), e = IntrinsicSet.end(); i != e; ++i) {
    OS << *i << "\n";
  }

  OS << SS.str();
}

unsigned BPLModuleWriter::nextCandidateNumber() {
  unsigned result = candidateNumber;
  candidateNumber++;
  return result;
}
