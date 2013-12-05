#include "llvm/ADT/StringExtras.h"
#include "llvm/Assembly/PrintModulePass.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/PassManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DataStream.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"

#include "bugle/BPLModuleWriter.h"
#include "bugle/IntegerRepresentation.h"
#include "bugle/Module.h"
#include "bugle/Preprocessing/CycleDetectPass.h"
#include "bugle/Preprocessing/InlinePass.h"
#include "bugle/Preprocessing/RemoveBodyPass.h"
#include "bugle/Preprocessing/RestrictDetectPass.h"
#include "bugle/RaceInstrumenter.h"
#include "bugle/Transform/SimplifyStmt.h"
#include "bugle/Translator/TranslateModule.h"
#include "bugle/util/ErrorReporter.h"

using namespace llvm;

static cl::opt<std::string>
InputFilename(cl::Positional, cl::desc("<input bitcode file>"),
    cl::init("-"), cl::value_desc("filename"));

static cl::opt<std::string>
OutputFilename("o", cl::desc("Override output filename"),
    cl::init(""), cl::value_desc("filename"));

static cl::opt<std::string>
GPUEntryPoints("k", cl::ZeroOrMore, cl::desc("GPU entry point function name"),
    cl::value_desc("function"));

static cl::opt<std::string>
SourceLanguage("l", cl::desc("Module source language (c, cu, cl; default c)"),
    cl::value_desc("language"));

static cl::opt<std::string>
IntegerRepresentation("i", cl::desc("Integer representation (bv, math; default bv)"),
    cl::value_desc("intrep"));

static cl::opt<bool>
Inlining("inline", cl::ValueDisallowed, cl::desc("Inline all function calls"));

static cl::opt<std::string>
RaceInstrumentation("race-instrumentation", cl::desc("Race instrumentation method to use (standard, watchdog-single, watchdog-multiple; default standard)"));

int main(int argc, char **argv) {
  sys::PrintStackTraceOnErrorSignal();
  llvm::PrettyStackTraceProgram X(argc, argv);

  // Enable debug stream buffering.
  EnableDebugBuffering = true;

  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.
  LLVMContext &Context = getGlobalContext();

  cl::ParseCommandLineOptions(argc, argv,
    "LLVM to Boogie translator\n");

  std::string DisplayFilename;
  if (InputFilename == "-")
    DisplayFilename = "<stdin>";
  else
    DisplayFilename = InputFilename;
  bugle::ErrorReporter::setFileName(DisplayFilename);

  std::string ErrorMessage;
  std::unique_ptr<Module> M;

  // Use the bitcode streaming interface
  DataStreamer *streamer = getDataFileStreamer(InputFilename, &ErrorMessage);
  if (streamer) {
    M.reset(getStreamedBitcodeModule(DisplayFilename, streamer, Context,
                                     &ErrorMessage));
    if (M.get() != 0 && M->MaterializeAllPermanently(&ErrorMessage)) {
      M.reset();
    }
  }

  if (M.get() == 0) {
    if (ErrorMessage.size())
      bugle::ErrorReporter::reportFatalError(ErrorMessage);
    else
      bugle::ErrorReporter::reportFatalError("Bitcode did not read correctly");
  }

  bugle::TranslateModule::SourceLanguage SL;
  if (SourceLanguage.empty() || SourceLanguage == "c")
    SL = bugle::TranslateModule::SL_C;
  else if (SourceLanguage == "cu")
    SL = bugle::TranslateModule::SL_CUDA;
  else if (SourceLanguage == "cl")
    SL = bugle::TranslateModule::SL_OpenCL;
  else {
    std::string msg = "Unsupported source language: " + SourceLanguage;
    bugle::ErrorReporter::reportParameterError(msg);
  }

  std::unique_ptr<bugle::IntegerRepresentation> IntRep;
  if (IntegerRepresentation.empty() || IntegerRepresentation == "bv")
    IntRep.reset(new bugle::BVIntegerRepresentation());
  else if (IntegerRepresentation == "math")
    IntRep.reset(new bugle::MathIntegerRepresentation);
  else {
    std::string msg = "Unsupported integer representation: "
      + IntegerRepresentation;
    bugle::ErrorReporter::reportParameterError(msg);
  }

  bugle::RaceInstrumenter RaceInst;
  if (RaceInstrumentation.empty() || RaceInstrumentation == "standard")
    RaceInst = bugle::RaceInstrumenter::STANDARD;
  else if (RaceInstrumentation == "watchdog-single")
    RaceInst = bugle::RaceInstrumenter::WATCHDOG_SINGLE;
  else if (RaceInstrumentation == "watchdog-multiple")
    RaceInst = bugle::RaceInstrumenter::WATCHDOG_MULTIPLE;
  else {
    std::string msg = "Unsupported race instrumentation: "
      + RaceInstrumentation;
    bugle::ErrorReporter::reportParameterError(msg);
  }

  std::set<std::string> EP;
  for (auto i = GPUEntryPoints.begin(), e = GPUEntryPoints.end(); i != e; ++i)
    EP.insert(&*i);

  PassManager PM;
  if (Inlining) {
    PM.add(new bugle::CycleDetectPass());
    PM.add(new bugle::InlinePass(SL, EP));
    PM.add(new bugle::RemoveBodyPass(M.get(), SL, EP));
  }
  PM.add(new bugle::RestrictDetectPass(M.get(), SL, EP));
  PM.run(*M.get());

  bugle::TranslateModule TM(M.get(), SL, EP, RaceInst);
  TM.translate();
  std::unique_ptr<bugle::Module> BM(TM.takeModule());

  bugle::simplifyStmt(BM.get());

  std::string OutFile = OutputFilename;
  if (OutFile.empty()) {
    SmallString<128> Path(InputFilename);
    sys::path::replace_extension(Path, "bpl");
    OutFile = sys::path::filename(Path);
  }

  std::string ErrorInfo;
  tool_output_file F(OutFile.c_str(), ErrorInfo);
  if (!ErrorInfo.empty())
    bugle::ErrorReporter::reportFatalError(ErrorInfo);

  bugle::BPLModuleWriter MW(F.os(), BM.get(), IntRep.get(), RaceInst);
  MW.write();

  F.keep();
  return 0;
}
