#ifndef LLVM_ANALYSIS_MYADVISOR_H
#define LLVM_ANALYSIS_MYADVISOR_H

#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
#include <random>
#include <sstream>
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
    DL = CB.getDebugLoc();
    assert(DL);
    DLoc = serializeDL();
    /*    if (auto* InlinedDL = DL.get()->getInlinedAt()) {
          auto* Tmp = DL.get()->getRawScope();
          if (auto* SP = dyn_cast<DISubprogram>(Tmp)) {
            CallerName = SP->getName();
          } else if (auto* SC = dyn_cast<DILexicalBlock>(Tmp)) {
            auto* scope = SC->getScope();
            CallerName = scope->getSubprogram()->getName();
          } else if (auto* LBF = dyn_cast<DILexicalBlockFile>(Tmp)) {
            auto* scope = LBF->getScope();
            CallerName = scope->getSubprogram()->getName();
          } else {
            DL.get()->getRawScope()->dump();
            exit(1);
          }
        } */
  }

  CallBaseDescr(const std::string &CallerName, const std::string &CalleeName,
                const std::string &DLoc)
      : CallerName(CallerName), CalleeName(CalleeName), DLoc(DLoc) {}

  bool operator==(const CallBaseDescr &other) const {
    return CalleeName == other.CalleeName && CallerName == other.CallerName &&
           DLoc == other.DLoc;
  }

  std::string serializeDL() const {
    std::stringstream ss;
    bool First = true;
    for (auto *IDL = DL.get(); IDL; IDL = IDL->getInlinedAt()) {
      if (!First)
        ss << "@";
      ss << IDL->getLine() << ":" << IDL->getColumn();
      First = false;
    }

    return ss.str();
  }

  std::string to_string() const {
    return CallerName + ":" + CalleeName + ":" + serializeDL();
  }

  std::string CallerName;
  std::string CalleeName;
  std::string DLoc;
  // unsigned Line;
  // unsigned Col;
  DebugLoc DL;
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

  void removeLastAdvice() { InlineConfig.pop_back(); }

private:
  std::mt19937 MTRand;
  std::vector<std::pair<CallBaseDescr, bool>> InlineConfig;
};

class TuneInlineAdvice : public InlineAdvice {
public:
  TuneInlineAdvice(StochasticInlineAdvisor *Advisor, CallBase &CB,
                   OptimizationRemarkEmitter &ORE, bool Advice)
      : InlineAdvice(dyn_cast<InlineAdvisor>(Advisor), CB, ORE, Advice),
        StochasticAdvisor(Advisor) {}

protected:
  void recordUnattemptedInliningImpl() override {}
  StochasticInlineAdvisor *StochasticAdvisor;
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
