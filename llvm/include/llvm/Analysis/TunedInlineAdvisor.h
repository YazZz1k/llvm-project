#ifndef LLVM_ANALYSIS_MYADVISOR_H
#define LLVM_ANALYSIS_MYADVISOR_H

#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <random>
#include <string>
#include <vector>

namespace llvm {

class OptimizationRemarkEmitter;
class StochasticInlineAdvice;

enum TunedAdvisorMode { PredConfig, StochasticConfig };

struct CallBaseDescr {
  // implicit
  CallBaseDescr(const CallBase &CB) {
    CallerName = CB.getCaller()->getName();
    CalleeName = CB.getCalledFunction()->getName();
    auto &DL = CB.getDebugLoc();
    assert(DL);
    Line = DL.getLine();
    Col = DL.getCol();
  }

  CallBaseDescr(const std::string &CallerName, const std::string &CalleeName,
                unsigned Line, unsigned Col)
      : CallerName(CallerName), CalleeName(CalleeName), Line(Line), Col(Col) {}

  bool operator==(const CallBaseDescr &other) const {
    return Line == other.Line && Col == other.Col;
  }

  std::string CallerName;
  std::string CalleeName;
  unsigned Line;
  unsigned Col;
};

class StochasticInlineAdvisor : public InlineAdvisor {
public:
  StochasticInlineAdvisor(Module &M, ModuleAnalysisManager &MAM, uint64_t Seed)
      : InlineAdvisor(
            M,
            MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager()),
        MTRand(Seed){};

  virtual ~StochasticInlineAdvisor();

  std::unique_ptr<InlineAdvice> getAdviceImpl(CallBase &CB) override;

public:
  void dumpConfig(StringRef OutputFile);
  void toJSON(json::OStream &OS);

  std::unique_ptr<OptimizationRemarkEmitter> ORE = nullptr;

private:
  std::mt19937 MTRand;
  std::vector<std::pair<CallBaseDescr, bool>> InlineConfig;
};

class PredefinedInlineAdvisor : public InlineAdvisor {
public:
  PredefinedInlineAdvisor(Module &M, ModuleAnalysisManager &MAM,
                          StringRef InputFile)
      : InlineAdvisor(
            M,
            MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager()) {
    parseInlineConfig(InputFile);
  };

  virtual ~PredefinedInlineAdvisor() = default;

  std::unique_ptr<InlineAdvice> getAdviceImpl(CallBase &CB) override;

public:
  std::unique_ptr<OptimizationRemarkEmitter> ORE = nullptr;

private:
  void parseInlineConfig(StringRef InputFile);
  std::vector<std::pair<CallBaseDescr, bool>> InlineConfig;
};

} // namespace llvm

#endif // LLVM_ANALYSIS_MYADVISOR_H
