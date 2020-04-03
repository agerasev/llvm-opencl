#include "CLIntrinsics.h"

#include <queue>

#include "llvm/IR/Intrinsics.h"
#include "llvm/ADT/APInt.h"

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
    std::string getTypeName(Type *Ty, bool isSigned=false) const {
      std::string str;
      raw_string_ostream out(str);
      printTypeName(out, Ty, isSigned);
      out.str();
      return str;
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
  class MemCopy : public IntrinsicGenerator {
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
  
  class CountLeadingZeros : public IntrinsicGenerator {
  public:
    void printContent(raw_ostream &Out) override {
      Out << "  return clz(a);\n";
    }
  };
  class CountTrailingZeros : public IntrinsicGenerator {
  public:
    void printContent(raw_ostream &Out) override {
      Out << "  return ctz(a);\n";
    }
  };

  class UAddWithOverflow : public IntrinsicGenerator {
  public:
    void printContent(raw_ostream &Out) override {
      Out << "  " << getTypeName(funT->getReturnType()) << " r;\n"
          << "  r.f0 = a + b;\n"
          << "  r.f1 = -(r.f0 < b);\n"
          << "  return r;\n";
    }
  };
  class SAddWithOverflow : public IntrinsicGenerator {
  public:
    void printContent(raw_ostream &Out) override {
      Type *Ty = funT->getParamType(0);
      std::string sty = getTypeName(Ty, true);

      Out << "  " << getTypeName(funT->getReturnType()) << " r;\n"
          << "  r.f0 = a + b;\n"
          << "  r.f1 = -(((" << sty << ")a >= 0) == ((" << sty << ")b >= 0) && " << 
             "((" + sty + ")r.f0 >= 0) != ((" << sty << ")a >= 0));\n"
          << "  return r;\n";
    }
  };
  class USubWithOverflow : public IntrinsicGenerator {
  public:
    void printContent(raw_ostream &Out) override {
      Out << "  " << getTypeName(funT->getReturnType()) << " r;\n"
          << "  r.f0 = a - b;\n"
          << "  r.f1 = -(r.f0 > a);\n"
          << "  return r;\n";
    }
  };
  class SSubWithOverflow : public IntrinsicGenerator {
  public:
    void printContent(raw_ostream &Out) override {
      Type *Ty = funT->getParamType(0);
      std::string sty = getTypeName(Ty, true);

      Out << "  " << getTypeName(funT->getReturnType()) << " r;\n"
          << "  r.f0 = a - b;\n"
          << "  r.f1 = -(((" << sty << ")a >= 0) != ((" << sty << ")b >= 0) && " <<
             "((" + sty + ")r.f0 >= 0) != ((" << sty << ")a >= 0));\n"
          << "  return r;\n";
    }
  };
  class UMulWithOverflow : public IntrinsicGenerator {
  public:
    void printContent(raw_ostream &Out) override {
      Type *Ty = funT->getParamType(0);
      int N = Ty->getIntegerBitWidth();

      Out << "  " << getTypeName(funT->getReturnType()) << " r;\n";

      Out << "  if (clz(a) + clz(b) + 2 <= " << N << ") {\n"
          << "    r.f0 = a*b;\n"
          << "    r.f1 = -1;\n"
          << "    return r;\n"
          << "  }\n";

      Out << "  r.f0 = (a >> 1)*b;\n"
          << "  r.f1 = -((" << getTypeName(Ty, true) << ")r.f0 < 0);\n"
          << "  r.f0 <<= 1;\n";

      Out << "  if (a & 1) {\n"
          << "    r.f0 += b;\n"
          << "    if (r.f0 < b) {\n"
          << "      r.f1 = -1;\n"
          << "    }\n"
          << "  }\n"
          << "  return r;\n";
    }
  };
  class SMulWithOverflow : public IntrinsicGenerator {
  public:
    void printContent(raw_ostream &Out) override {
      Type *Ty = funT->getParamType(0);
      std::string sty = getTypeName(Ty, true);

      Out << "  " << getTypeName(funT->getReturnType()) << " r;\n";

      Out << "  r.f0 = a*b;\n"
          << "  if (a != 0 && b != 0) {\n"
          << "    r.f1 = -((" << sty << ")r.f0/(" << sty << ")b != (" << sty << ")a || " <<
             "(" << sty << ")r.f0/(" << sty << ")a != (" << sty << ")b);\n"
          << "  } else {\n"
          << "    r.f0 = 0;\n"
          << "  }\n"
          << "  return r;\n";
    }
  };
  /*
  class SDivWithOverflow : public IntrinsicGenerator {
  public:
    void printContent(raw_ostream &Out) override {
      Type *Ty = funT->getParamType(0);
      int N = Ty->getIntegerBitWidth();
      std::string uty = getTypeName(Ty, false),
                  sty = getTypeName(Ty, true);
      
      Out << "  " << getTypeName(funT->getReturnType()) << " r;\n"
          << "  r.f0 = (" << uty << ")((" << sty << ")a/(" << sty << ")b);\n"
          << "  r.f1 = (a == ((" << uty << ")1 << " << (N - 1) << ") && " <<
             "(b == ~(" << uty << ")0);\n"
          << "  return r;\n"
    }
  };
  */

  static std::pair<unsigned, std::unique_ptr<CLIntrinsic>> make_entry(unsigned Opcode, IntrinsicGenerator *gen) {
    return std::make_pair(Opcode, std::unique_ptr<CLIntrinsic>(new IntrinsicGeneratorWraper(gen)));
  }

  void CLIntrinsicMap::insert(std::pair<unsigned, std::unique_ptr<CLIntrinsic>> &&value) {
    intrinsics.insert(std::move(value));
  }

  CLIntrinsicMap::CLIntrinsicMap() {
    insert(make_entry(Intrinsic::memset, new MemSet()));
    insert(make_entry(Intrinsic::memcpy, new MemCopy()));
    insert(make_entry(Intrinsic::fmuladd, new FMulAdd()));
    insert(make_entry(Intrinsic::ctlz, new CountLeadingZeros()));
    insert(make_entry(Intrinsic::cttz, new CountTrailingZeros()));
    insert(make_entry(Intrinsic::uadd_with_overflow, new UAddWithOverflow()));
    insert(make_entry(Intrinsic::sadd_with_overflow, new SAddWithOverflow()));
    insert(make_entry(Intrinsic::usub_with_overflow, new USubWithOverflow()));
    insert(make_entry(Intrinsic::ssub_with_overflow, new SSubWithOverflow()));
    insert(make_entry(Intrinsic::umul_with_overflow, new UMulWithOverflow()));
    insert(make_entry(Intrinsic::smul_with_overflow, new SMulWithOverflow()));
    //insert(make_entry(Intrinsic::sdiv_with_overflow, new SDivWithOverflow()));
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
    return get(Opcode) != nullptr;
  }
} // namespace llvm_opencl  
