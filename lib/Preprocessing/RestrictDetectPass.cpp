#include "bugle/Preprocessing/RestrictDetectPass.h"
#include "bugle/Translator/TranslateFunction.h"
#include "bugle/Translator/TranslateModule.h"
#include "bugle/util/ErrorReporter.h"
#include "llvm/DebugInfo.h"
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Path.h"

using namespace llvm;
using namespace bugle;

std::string RestrictDetectPass::getFunctionLocation(llvm::Function *F) {
  for (auto i = DIF.subprogram_begin(), e = DIF.subprogram_end(); i != e; ++i) {
    DISubprogram subprogram(*i);
    if (subprogram.describes(F)) {
      SmallVector<char, 0> path;
      sys::path::append(path, subprogram.getDirectory());
      sys::path::append(path, subprogram.getFilename());
      return "'" + subprogram.getName().str() + "' at line "
        + std::to_string(subprogram.getLineNumber()) + " of "
        + path.data();
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
