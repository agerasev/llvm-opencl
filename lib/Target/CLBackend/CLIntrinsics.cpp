#include "CLIntrinsics.h"

#include <queue>

#include "llvm/IR/Intrinsics.h"

#include "StringTools.h"


namespace llvm_opencl {
  using namespace llvm;

  class IntrinsicGenerator {
  protected:
    raw_ostream *OutPtr;
    FunctionType *funT;
    std::string OpName;
    std::function<void(raw_ostream&, Type*, bool)> printTy;

  public:
    virtual ~IntrinsicGenerator() = default;

    void set(
      raw_ostream &Out, FunctionType *funT,
      std::string OpName, std::function<void(raw_ostream&, Type*, bool)> printTy
    ) {
      this->OutPtr = &Out;
      this->funT = funT;
      this->OpName = OpName;
      this->printTy = printTy;
    }

    void printTypeName(raw_ostream &Out, Type *Ty, bool isSigned=false) const {
      printTy(Out, Ty, isSigned);
    }

    virtual void printContent(raw_ostream &Out) = 0;
    virtual void printBefore(raw_ostream &Out) {}
    virtual void printAfter(raw_ostream &Out) {}

    void printDefinition() {
      Type *retT = funT->getReturnType();
      int numParams = funT->getNumParams();
      raw_ostream &Out = *OutPtr;

      printBefore(Out);
      
      Out << "static ";
      printTypeName(Out, retT);
      Out << " ";
      Out << OpName;
      Out << "(";
      for (int i = 0; i < numParams; i++) {
        printTypeName(Out, funT->getParamType(i));
        // FIXME_: Use numbers instead of letters
        Out << " " << (char)('a' + i);
        if (i != numParams - 1)
          Out << ", ";
      }
      Out << ") {\n";

      printContent(Out);

      Out << "}\n";

      printAfter(Out);
    }
  };

  class IntrinsicGeneratorWraper : public CLIntrinsic {
  private:
    mutable std::unique_ptr<IntrinsicGenerator> generator;
  public:
    IntrinsicGeneratorWraper(IntrinsicGenerator *gen) : generator(gen) {}
    void printDefinition(
      raw_ostream &Out, FunctionType *funT,
      std::string OpName, std::function<void(raw_ostream&, Type*, bool)> printTy
    ) const override {
      generator->set(Out, funT, OpName, printTy);
      generator->printDefinition();
    }
  };

  class MemSet : public IntrinsicGenerator {
  public:
    void printContent(raw_ostream &Out) override {
      Out << "  for (uint i = 0; i < c; ++i) {\n"
          << "    a[i] = b;\n"
          << "  }\n";
    }
  };

  class MemCpy : public IntrinsicGenerator {
  public:
    void printContent(raw_ostream &Out) override {
      Out << "  for (uint i = 0; i < c; ++i) {\n"
          << "    a[i] = b[i];\n"
          << "  }\n";
    }
  };

  class FMulAdd : public IntrinsicGenerator {
  public:
    void printContent(raw_ostream &Out) override {
      Out << "  return fma(a, b, c);\n";
    }
  };

  static std::pair<unsigned, std::unique_ptr<CLIntrinsic>> make_entry(unsigned Opcode, IntrinsicGenerator *gen) {
    return std::make_pair(Opcode, std::unique_ptr<CLIntrinsic>(new IntrinsicGeneratorWraper(gen)));
  }

  void CLIntrinsicMap::insert(std::pair<unsigned, std::unique_ptr<CLIntrinsic>> &&value) {
    intrinsics.insert(std::move(value));
  }

  CLIntrinsicMap::CLIntrinsicMap() {
    insert(make_entry(Intrinsic::memset, new MemSet()));
    insert(make_entry(Intrinsic::memcpy, new MemCpy()));
    insert(make_entry(Intrinsic::fmuladd, new FMulAdd()));
    //insert(make_entry(Intrinsic::smul_with_overflow, new SMulWithOverflow()));
  }

  const CLIntrinsic *CLIntrinsicMap::get(unsigned Opcode) const {
    auto search = intrinsics.find(Opcode);
    if (search != intrinsics.end()) {
      return &*(search->second);
    } else {
      return nullptr;
    }
  }

  bool CLIntrinsicMap::hasImpl(unsigned Opcode) const {
    switch (Opcode) {
    case Intrinsic::not_intrinsic:
    case Intrinsic::dbg_value:
    case Intrinsic::dbg_declare:
      return true;
    default:
      return get(Opcode) != nullptr;
    }
  }
} // namespace llvm_opencl  
