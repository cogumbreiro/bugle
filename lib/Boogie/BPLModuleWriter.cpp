#include "bugle/BPLModuleWriter.h"
#include "bugle/BPLFunctionWriter.h"
#include "bugle/IntegerRepresentation.h"
#include "bugle/Module.h"
#include "bugle/RaceInstrumenter.h"
#include "bugle/Type.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cmath>

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
  case Type::FunctionPointer:
    UsesFunctionPointers = true;
    OS << "functionPtr";
    break;
  case Type::Any:
  case Type::Unknown:
    llvm_unreachable("Module writer found unexpected type");
  }
}

void BPLModuleWriter::writeIntrinsic(std::function<void(llvm::raw_ostream &)> F,
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
    for (auto i = M->global_init_begin(), e = M->global_init_end(); i != e;
         ++i) {
      SS << "requires "
         << "$$" << i->array->getName() << "["
         << MW->IntRep->getLiteral(i->offset, M->getPointerWidth())
         << "] == ";
      writeExpr(SS, i->init.get());
      SS << ";\n";
    }
  }
  return GlobalInitRequires;
}

void BPLModuleWriter::write() {
  std::string S;
  llvm::raw_string_ostream SS(S);
  for (auto i = M->function_begin(), e = M->function_end(); i != e; ++i) {
    BPLFunctionWriter FW(this, SS, *i);
    FW.write();
  }
  for (auto i = M->axiom_begin(), e = M->axiom_end(); i != e; ++i) {
    SS << "axiom ";
    writeExpr(SS, i->get());
    SS << ";\n";
  }

  OS << "type _SIZE_T_TYPE = bv" << M->getPointerWidth() << ";\n\n";

  if (UsesPointers) {
    if (RepresentPointersAsDatatype) {
      OS << "type {:datatype} ptr;\n"
         << "type arrayId;\n"
         << "function {:constructor} MKPTR(base: arrayId, offset: "
         << MW->IntRep->getType(M->getPointerWidth())
         << ") : ptr;\n\n";
    } else {
      // We reserve an array base value for "null", and a value for
      // "undefined"
      const unsigned NumberOfSpecialArrayBaseValues = 2;
      unsigned BitsRequiredForArrayBases = (unsigned)std::ceil(
          std::log(
              (double)(M->global_size() + NumberOfSpecialArrayBaseValues)) /
          std::log((double)2));
      OS << "type ptr = bv"
         << (M->getPointerWidth() + BitsRequiredForArrayBases) << ";\n"
         << "type arrayId = bv" << BitsRequiredForArrayBases << ";\n"
         << "function {:inline true} MKPTR(base: arrayId, offset: "
         << MW->IntRep->getType(M->getPointerWidth())
         << ") : ptr {\n"
         << "  base ++ offset\n"
         << "}\n\n"
         << "function {:inline true} base#MKPTR(p: ptr) : arrayId {\n"
         << "  p[" << (M->getPointerWidth() + BitsRequiredForArrayBases)
         << ":" << M->getPointerWidth() << "]\n"
         << "}\n\n"
         << "function {:inline true} offset#MKPTR(p : ptr) : bv"
         << M->getPointerWidth() << "{\n"
         << "  p[" << M->getPointerWidth() << ":0]\n"
         << "}\n\n";
    }
  }

  unsigned long int sizes = 0;
  for (auto i = M->global_begin(), e = M->global_end(); i != e; ++i) {
    unsigned long int size = (1 << (((*i)->getRangeType().width / 8)));
    if (!(size & sizes)) {
      auto pw = IntRep->getType(M->getPointerWidth());
      auto bw = IntRep->getType((*i)->getRangeType().width);
      OS << "procedure _ATOMIC_OP" << (*i)->getRangeType().width << "(x : ["
         << pw << "]" << bw << ", y : "
         << pw << ") returns (z : " << bw << ", A : ["
         << pw << "]" << bw << ");\n";
      sizes = size | sizes;
    }
  }

  for (auto i = M->global_begin(), e = M->global_end(); i != e; ++i) {
    OS << "var ";
    OS << "{:original_name \"" << (*i)->getOriginalName() << "\"} ";
    for (auto ai = (*i)->attrib_begin(), ae = (*i)->attrib_end(); ai != ae;
         ++ai) {
      OS << "{:" << *ai << "} ";
    }
    OS << "$$" << (*i)->getName() << " : ["
       << IntRep->getType(M->getPointerWidth())
       << "]";
    writeType(OS, (*i)->getRangeType());
    OS << ";\n";

    if ((*i)->isGlobalOrGroupShared()) {
      std::string attributes = " {:race_checking} ";
      if ((*i)->isGlobal())
        attributes += "{:global} ";
      else if ((*i)->isGroupShared())
        attributes += "{:group_shared} ";

      OS << "var" << attributes << "{:elem_width " << (*i)->getRangeType().width
         << "} "
         << "_READ_HAS_OCCURRED_$$" << (*i)->getName() << " : bool;\n";
      OS << "var" << attributes << "{:elem_width " << (*i)->getRangeType().width
         << "} "
         << "_WRITE_HAS_OCCURRED_$$" << (*i)->getName() << " : bool;\n";
      OS << "var" << attributes << "{:elem_width " << (*i)->getRangeType().width
         << "} "
         << "_ATOMIC_HAS_OCCURRED_$$" << (*i)->getName() << " : bool;\n";

      switch (RaceInst) {
      case RaceInstrumenter::Standard:
        OS << "var" << attributes << "_READ_OFFSET_$$" << (*i)->getName()
           << " : " << IntRep->getType(M->getPointerWidth()) << ";\n";
        OS << "var" << attributes << "_WRITE_OFFSET_$$" << (*i)->getName()
           << " : " << IntRep->getType(M->getPointerWidth()) << ";\n";
        OS << "var" << attributes << "_ATOMIC_OFFSET_$$" << (*i)->getName()
           << " : " << IntRep->getType(M->getPointerWidth()) << ";\n";
        break;
      case RaceInstrumenter::WatchdogMultiple:
        OS << "const" << attributes << "_WATCHED_OFFSET_$$" << (*i)->getName()
           << " : " << IntRep->getType(M->getPointerWidth()) << ";\n";
        break;
      case RaceInstrumenter::WatchdogSingle:
        // No output in this case: below we output the single watched offset
        break;
      }
    }

    if (UsesPointers)
      OS << "const unique $arrayId$$" << (*i)->getName() << " : arrayId;\n";

    OS << "\n";
  }

  if (RaceInst == RaceInstrumenter::WatchdogSingle)
    OS << "const _WATCHED_OFFSET : " << IntRep->getType(M->getPointerWidth())
       << ";\n";

  if (UsesPointers)
    OS << "const unique $arrayId$$null$ : arrayId;\n\n";

  if (UsesFunctionPointers) {
    OS << "type functionPtr";
    if (!RepresentPointersAsDatatype) {
      // We reserve a function pointer value for "null", and a value for
      // "undefined"
      const unsigned NumberOfSpecialFunctionPointerValues = 2;
      unsigned BitsRequiredForFunctionPointers = (unsigned)std::ceil(
          std::log((double)(M->function_size() +
                            NumberOfSpecialFunctionPointerValues)) /
          std::log((double)2));
      OS << " = bv" << BitsRequiredForFunctionPointers;
    }
    OS << ";\n";

    for (auto i = M->function_begin(), e = M->function_end(); i != e; ++i) {
      OS << "const unique $functionId$$" << (*i)->getName()
         << " : functionPtr;\n";
    }

    OS << "const unique $functionId$$null$ : functionPtr;\n\n";
  }

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
