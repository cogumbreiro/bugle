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
  if (NamedMDNode *CU_Nodes = M.getNamedMetadata("llvm.dbg.cu"))
    DITyMap = generateDITypeIdentifierMap(CU_Nodes);
  return false;
}

const MDSubprogram *RestrictDetectPass::getDebugInfo(llvm::Function *F) {
  auto SS = DIF.subprograms();
  for (auto i = SS.begin(), e = SS.end(); i != e; ++i) {
    if ((*i)->describes(F))
      return *i;
  }

  return 0;
}

std::string RestrictDetectPass::getFunctionLocation(llvm::Function *F) {
  auto MDS = getDebugInfo(F);
  if (MDS) {
    std::string l; llvm::raw_string_ostream lS(l);
    lS << "'" << MDS->getName() << "' on line " << MDS->getLine()
       << " of " << MDS->getFilename();
    return lS.str();
  } else
    return "'" + F->getName().str() + "'";
}

bool RestrictDetectPass::ignoreArgument(unsigned i, const MDSubprogram *MDS) {
  if (!MDS || SL != TranslateModule::SL_OpenCL)
    return false;

  auto Ty = MDS->getType()->getTypeArray()[i + 1].resolve(DITyMap)->getName();
  return (Ty == "__bugle_image2d_t" || Ty == "__bugle_image3d_t");
}

void RestrictDetectPass::doRestrictCheck(llvm::Function &F) {
  auto *MDS = getDebugInfo(&F);
  std::vector<Argument *> AL;
  for (auto i = F.arg_begin(), e = F.arg_end(); i != e; ++i) {
    if (!i->getType()->isPointerTy())
      continue;
    if (i->hasNoAliasAttr())
      continue;
    if (i->getType()->getPointerElementType()->isFunctionTy())
      continue;
    if (ignoreArgument(i->getArgNo(), MDS))
      continue;

    unsigned addressSpace = i->getType()->getPointerAddressSpace();
    if (addressSpace == AddressSpaces.generic &&
        SL == TranslateModule::SL_CUDA)
      AL.push_back(i);

    if (addressSpace == AddressSpaces.global)
      AL.push_back(i);
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
