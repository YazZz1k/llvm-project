#include "llvm/Analysis/TunedInlineAdvisor.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"

#define DEBUG_TYPE "stochastic-inline-adviser"

using namespace llvm;

static cl::opt<TunedAdvisorMode>
    Mode("set-mode", cl::init(TunedAdvisorMode::StochasticConfig), cl::Hidden,
         cl::desc("set mode for tuned advisor"),
         cl::values(clEnumValN(TunedAdvisorMode::PredConfig, "pred",
                               "use predefined config from file"),
                    clEnumValN(TunedAdvisorMode::StochasticConfig, "rand",
                               "emit random inline config")));

static cl::opt<uint64_t> Seed("seed", cl::init(time(0)), cl::Hidden,
                              cl::desc("seed for stochastic inlining"));

static cl::opt<std::string>
    FileName("file", cl::init("file.json"), cl::Hidden,
             cl::desc("file to load/store inlining config"));

std::unique_ptr<InlineAdvisor> llvm::getTunedModeAdvisor(
    Module &M, ModuleAnalysisManager &MAM,
    [[maybe_unused]] std::function<bool(CallBase &)> GetDefaultAdvice) {
  switch (Mode) {
  case TunedAdvisorMode::PredConfig:
    dbgs() << "Use Predifined advisor\n";
    return std::make_unique<PredefinedInlineAdvisor>(M, MAM, FileName);
  case TunedAdvisorMode::StochasticConfig:
    dbgs() << "Use Stochastic advisor\n";
    return std::make_unique<StochasticInlineAdvisor>(M, MAM, Seed);
  }
  return nullptr;
}

std::unique_ptr<InlineAdvice>
PredefinedInlineAdvisor::getAdviceImpl(CallBase &CB) {
  ORE = std::make_unique<OptimizationRemarkEmitter>(CB.getCaller());
  auto it = std::find_if(InlineConfig.begin(), InlineConfig.end(),
                         [&CB](const auto &Edge) { return Edge.first == CB; });
  if (it == InlineConfig.end()) {
    CallBaseDescr CBD(CB);
    errs() << "Cant find the config for edge:\n";
    errs() << CBD.CallerName << ":" << CBD.CalleeName << ":" << CBD.Line << ":" << CBD.Col << "\n";
    exit(1);
  }
  return std::make_unique<InlineAdvice>(this, CB, *ORE, (*it).second);
}

std::unique_ptr<InlineAdvice>
StochasticInlineAdvisor::getAdviceImpl(CallBase &CB) {
  ORE = std::make_unique<OptimizationRemarkEmitter>(CB.getCaller());
  bool RandAdvice = std::uniform_int_distribution<>{0, 1}(MTRand);
  InlineConfig.push_back({CallBaseDescr(CB), RandAdvice});
  return std::make_unique<InlineAdvice>(this, CB, *ORE, RandAdvice);
}

void StochasticInlineAdvisor::dumpConfig(StringRef OutputFile) {
  std::error_code EC;
  raw_fd_ostream OFile(OutputFile, EC, llvm::sys::fs::OF_None);
  if (EC) {
    errs() << "error: can't open file: " + FileName;
    exit(1);
  }
  json::OStream JOS(OFile);
  toJSON(JOS);
}

void StochasticInlineAdvisor::toJSON(json::OStream &JOS) {
  JOS.object([&]() {
    JOS.attributeArray("CGEdges", [&]() {
      for (const auto &Decision : InlineConfig) {
        const auto &CB = Decision.first;
        bool Advice = Decision.second;
        JOS.object([&]() {
          JOS.attribute("caller", CB.CallerName);
          JOS.attribute("callee", CB.CalleeName);
          JOS.attributeArray("loc", [&]() {
            JOS.value(CB.Line);
            JOS.value(CB.Col);
          });
          JOS.attribute("advice", Advice);
        });
      }
    });
  });
}

StochasticInlineAdvisor::~StochasticInlineAdvisor() { dumpConfig(FileName); }

static std::pair<CallBaseDescr, bool>
getEdgeConfigFromJSON(const json::Value &Value) {
  const auto *Obj = Value.getAsObject();
  const auto *caller = Obj->get("caller");
  const auto *callee = Obj->get("callee");
  const auto loc = *Obj->get("loc")->getAsArray();
  const auto *advice = Obj->get("advice");

  std::string CallerName = (*caller->getAsString()).data();
  std::string CalleeName = (*callee->getAsString()).data();
  unsigned Line = *(loc[0].getAsUINT64());
  unsigned Col = *(loc[1].getAsUINT64());
  bool Advice = *advice->getAsBoolean();
  return {CallBaseDescr(CallerName, CalleeName, Line, Col), Advice};
}

// TODO: add handling the parse errors
void PredefinedInlineAdvisor::parseInlineConfig(StringRef InputFile) {
  auto BufferOrError = MemoryBuffer::getFileOrSTDIN(InputFile);
  if (!BufferOrError) {
    errs() << "Cant open the file " << InputFile << "\n";
    exit(1);
  }

  auto ParsedJSONValues = json::parse(BufferOrError.get()->getBuffer());
  if (!ParsedJSONValues) {
    errs() << "Can't parse the JSON\n";
    exit(1);
  }

  const auto *ArrObj = ParsedJSONValues->getAsObject();
  const auto *CGEdges = ArrObj->get("CGEdges")->getAsArray();

  for (const auto &Value : *CGEdges) {
    auto [Edge, Advice] = getEdgeConfigFromJSON(Value);
    InlineConfig.push_back({Edge, Advice});
  }
}
