#include "bugle/Preprocessing/RestrictDetectPass.h"
#include "bugle/Translator/TranslateFunction.h"
#include "bugle/Translator/TranslateModule.h"
#include "bugle/util/ErrorReporter.h"
#include "llvm/Pass.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace bugle;

bool RestrictDetectPass::doInitialization(llvm::Module &M) {
  this->M = &M;
  DIF.processModule(M);
  return false;
}

std::string RestrictDetectPass::getFunctionLocation(llvm::Function *F) {
  auto SS = DIF.subprograms();
  for (auto i = SS.begin(), e = SS.end(); i != e; ++i) {
    DISubprogram subprogram(*i);
    if (subprogram.describes(F)) {
      std::string l; llvm::raw_string_ostream lS(l);
      lS << "'" << subprogram.getName() << "' on line "
         << subprogram.getLineNumber() << " of " << subprogram.getFilename();
      return lS.str();
    }
  }

  return "'" + F->getName().str() + "'";
}

void RestrictDetectPass::doRestrictCheck(llvm::Function &F) {
  std::vector<Argument *> AL;
  for (auto i = F.arg_begin(), e = F.arg_end(); i != e; ++i) {
    if (!i->getType()->isPointerTy())
      continue;
    if (i->hasNoAliasAttr())
      continue;
    if (i->getType()->getPointerElementType()->isFunctionTy())
      continue;

    switch (i->getType()->getPointerAddressSpace()) {
    case TranslateModule::AddressSpaces::standard:
      if (SL == TranslateModule::SL_CUDA)
        AL.push_back(i);
      break;
    case TranslateModule::AddressSpaces::global:
      AL.push_back(i);
      break;
    default:
      break;
    }
  }

  if (AL.size() <= 1)
    return;

  std::string msg = "Assuming the arguments ";

  auto i = AL.begin(), e = AL.end();
  do {
    msg += "'" + (*i)->getName().str() + "'";
    ++i;
    if (i != e)
      msg += ", ";
  } while (i != e);

  std::string name = getFunctionLocation(&F);
  msg += " of " + name + " to be non-aliased; ";
  msg += "please consider adding a restrict qualifier to these arguments";
  ErrorReporter::emitWarning(msg);
}

bool RestrictDetectPass::runOnFunction(llvm::Function &F) {
  if (!(SL == TranslateModule::SL_OpenCL || SL == TranslateModule::SL_CUDA))
    return false;
  if (!TranslateFunction::isNormalFunction(SL, &F))
    return false;
  if (!TranslateModule::isGPUEntryPoint(&F, M, SL, GPUEntryPoints))
    return false;

  doRestrictCheck(F);

  return false;
}

char RestrictDetectPass::ID = 0;
