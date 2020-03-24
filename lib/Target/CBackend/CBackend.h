#include "CTargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/CodeGen/IntrinsicLowering.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Pass.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Transforms/Scalar.h"

#include <set>
#include <functional>

#include "IDMap.h"
#include "CLBuiltIns.h"

namespace llvm_cbe {

using namespace llvm;

class CBEMCAsmInfo : public MCAsmInfo {
public:
  CBEMCAsmInfo() { PrivateGlobalPrefix = ""; }
};

/// CWriter - This class is the main chunk of code that converts an LLVM
/// module to a C translation unit.
class CWriter : public FunctionPass, public InstVisitor<CWriter> {
  std::string _Out;
  std::string _OutHeaders;
  raw_string_ostream OutHeaders;
  raw_string_ostream Out;
  raw_ostream &FileOut;
  IntrinsicLowering *IL = nullptr;
  LoopInfo *LI = nullptr;
  const Module *TheModule = nullptr;
  const MCAsmInfo *TAsm = nullptr;
  const MCRegisterInfo *MRI = nullptr;
  const MCObjectFileInfo *MOFI = nullptr;
  MCContext *TCtx = nullptr;
  const DataLayout *TD = nullptr;
  const Instruction *CurInstr = nullptr;

  std::set<const Argument *> ByValParams;

  IDMap<const Value *> AnonValueNumbers;

  /// UnnamedStructIDs - This contains a unique ID for each struct that is
  /// either anonymous or has no name.
  IDMap<StructType *> UnnamedStructIDs;

  std::set<Type *> TypedefDeclTypes;
  std::set<Type *> SelectDeclTypes;
  std::set<std::pair<CmpInst::Predicate, Type *>> CmpDeclTypes;
  std::set<std::pair<CastInst::CastOps, std::pair<Type *, Type *>>>
      CastOpDeclTypes;
  std::set<std::pair<unsigned, Type *>> InlineOpDeclTypes;
  std::set<Type *> CtorDeclTypes;

  IDMap<std::pair<FunctionType *, std::pair<AttributeList, CallingConv::ID>>>
      UnnamedFunctionIDs;

  // This is used to keep track of intrinsics that get generated to a lowered
  // function. We must generate the prototypes before the function body which
  // will only be expanded on first use
  std::vector<Function *> prototypesToGen;

  unsigned LastAnnotatedSourceLine = 0;

  struct {
    bool UnalignedLoad : 1;
    bool BitCastUnion : 1;
  } UsedHeaders;

  CLBuiltIns builtins;

#define USED_HEADERS_FLAG(Name)                                                \
  void headerUse##Name() { UsedHeaders.Name = true; }                          \
  bool headerInc##Name() const { return UsedHeaders.Name; }

  USED_HEADERS_FLAG(UnalignedLoad)
  USED_HEADERS_FLAG(BitCastUnion)

  void generateCompilerSpecificCode(raw_ostream &Out, const DataLayout *) const;

public:
  static char ID;
  explicit CWriter(raw_ostream &o)
      : FunctionPass(ID), OutHeaders(_OutHeaders), Out(_Out), FileOut(o) {
    memset(&UsedHeaders, 0, sizeof(UsedHeaders));
  }

  virtual StringRef getPassName() const { return "C backend"; }

  void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<LoopInfoWrapperPass>();
    AU.setPreservesCFG();
  }

  virtual bool doInitialization(Module &M);
  virtual bool doFinalization(Module &M);
  virtual bool runOnFunction(Function &F);

private:
  void generateHeader(Module &M);
  void declareOneGlobalVariable(GlobalVariable *I);

  void forwardDeclareStructs(raw_ostream &Out, Type *Ty,
                             std::set<Type *> &TypesPrinted);

  raw_ostream &
  printFunctionProto(raw_ostream &Out, FunctionType *Ty,
                     std::pair<AttributeList, CallingConv::ID> Attrs,
                     const std::string &Name,
                     iterator_range<Function::arg_iterator> *ArgList);
  raw_ostream &printFunctionProto(raw_ostream &Out, Function *F) {
    return printFunctionProto(
        Out, F->getFunctionType(),
        std::make_pair(F->getAttributes(), F->getCallingConv()),
        GetValueName(F), nullptr);
  }

  raw_ostream &
  printFunctionDeclaration(raw_ostream &Out, FunctionType *Ty,
                           std::pair<AttributeList, CallingConv::ID> PAL =
                               std::make_pair(AttributeList(), CallingConv::C));
  raw_ostream &printStructDeclaration(raw_ostream &Out, StructType *Ty);
  raw_ostream &printArrayDeclaration(raw_ostream &Out, ArrayType *Ty);
  raw_ostream &printVectorDeclaration(raw_ostream &Out, VectorType *Ty);

  void printWithCast(raw_ostream &Out, Type *DstTy, bool isSigned,
                     std::function<void()> print_inner);
  void printWithCast(raw_ostream &Out, Type *DstTy, bool isSigned,
                     const std::string &inner);
  void printWithCastIf(raw_ostream &Out, Type *DstTy, bool isSigned,
                       std::function<void()> print_inner, bool cond);
  void printWithCastIf(raw_ostream &Out, Type *DstTy, bool isSigned,
                       const std::string &inner, bool cond);
  void printIntSExt(raw_ostream &Out, Type *Ty, const std::string &var);
  void printIntTrunc(raw_ostream &Out, Type *Ty, const std::string &var);

  raw_ostream &printTypeName(raw_ostream &Out, Type *Ty, bool isSigned = false,
                             std::pair<AttributeList, CallingConv::ID> PAL =
                                 std::make_pair(AttributeList(),
                                                CallingConv::C));
  raw_ostream &printSimpleType(raw_ostream &Out, Type *Ty, bool isSigned=false);
  raw_ostream &printTypeString(raw_ostream &Out, Type *Ty);

  std::string getStructName(StructType *ST);
  std::string getFunctionName(FunctionType *FT,
                              std::pair<AttributeList, CallingConv::ID> PAL =
                                  std::make_pair(AttributeList(),
                                                 CallingConv::C));
  std::string getArrayName(ArrayType *AT);
  std::string getVectorName(VectorType *VT, bool Aligned, bool isSigned=false);

  raw_ostream &printVectorComponent(raw_ostream &Out, uint64_t i);
  raw_ostream &printVectorShuffled(raw_ostream &Out, const std::vector<uint64_t> &mask);

  enum OperandContext {
    ContextNormal,
    ContextStatic
    // Static context means that it is being used in as a static initializer
  };

  void writeOperandDeref(Value *Operand);
  void writeOperand(Value *Operand,
                    enum OperandContext Context = ContextNormal);
  void writeInstComputationInline(Instruction &I);
  void writeOperandInternal(Value *Operand,
                            enum OperandContext Context = ContextNormal);
  void writeOperandWithCast(Value *Operand, unsigned Opcode);
  void opcodeNeedsCast(unsigned Opcode, bool &shouldCast, bool &castIsSigned);

  void writeOperandWithCast(Value *Operand, ICmpInst &I);
  bool writeInstructionCast(Instruction &I);
  void writeMemoryAccess(Value *Operand, Type *OperandType, bool IsVolatile,
                         unsigned Alignment);

  bool lowerIntrinsics(Function &F);
  /// Prints the definition of the intrinsic function F. Supports the
  /// intrinsics which need to be explicitly defined in the CBackend.
  void printIntrinsicDefinition(Function &F, raw_ostream &Out);
  void printIntrinsicDefinition(FunctionType *funT, unsigned Opcode,
                                std::string OpName, raw_ostream &Out);

  void printModuleTypes(raw_ostream &Out);
  void printContainedTypes(raw_ostream &Out, Type *Ty, std::set<Type *> &);

  void printFPConstantValue(raw_ostream &Out, const ConstantFP *FPC,
                            enum OperandContext Context=ContextNormal);

  void printFunction(Function &);
  void printBasicBlock(BasicBlock *BB);
  void printLoop(Loop *L);

  void printCast(unsigned opcode, Type *SrcTy, Type *DstTy);
  void printConstant(Constant *CPV, enum OperandContext Context=ContextNormal);
  void printConstantWithCast(Constant *CPV, unsigned Opcode);
  void printConstantArray(ConstantArray *CPA, enum OperandContext Context);
  void printConstantVector(ConstantVector *CV, enum OperandContext Context);
  void printConstantDataSequential(ConstantDataSequential *CDS,
                                   enum OperandContext Context);
  bool printConstantString(Constant *C, enum OperandContext Context);
  

  bool isEmptyType(Type *Ty) const;
  bool isAddressExposed(Value *V) const;
  bool isInlinableInst(Instruction &I) const;
  AllocaInst *isDirectAlloca(Value *V) const;

  // Instruction visitation functions
  friend class InstVisitor<CWriter>;

  void visitReturnInst(ReturnInst &I);
  void visitBranchInst(BranchInst &I);
  void visitSwitchInst(SwitchInst &I);
  void visitIndirectBrInst(IndirectBrInst &I);
  void visitInvokeInst(InvokeInst &I) {
    llvm_unreachable("Lowerinvoke pass didn't work!");
  }
  void visitResumeInst(ResumeInst &I) {
    llvm_unreachable("DwarfEHPrepare pass didn't work!");
  }

  void visitPHINode(PHINode &I);
  void visitBinaryOperator(BinaryOperator &I);
  void visitICmpInst(ICmpInst &I);
  void visitFCmpInst(FCmpInst &I);

  void visitCastInst(CastInst &I);
  void visitSelectInst(SelectInst &I);
  void visitCallInst(CallInst &I);
  bool visitBuiltinCall(CallInst &I, Intrinsic::ID ID);

  void visitLoadInst(LoadInst &I);
  void visitStoreInst(StoreInst &I);
  void visitFenceInst(FenceInst &I);
  void visitGetElementPtrInst(GetElementPtrInst &I);
  void visitVAArgInst(VAArgInst &I);

  void visitInsertElementInst(InsertElementInst &I);
  void visitExtractElementInst(ExtractElementInst &I);
  void visitShuffleVectorInst(ShuffleVectorInst &SVI);

  void visitInsertValueInst(InsertValueInst &I);
  void visitExtractValueInst(ExtractValueInst &I);

  void visitInstruction(Instruction &I) {
    CurInstr = &I;
    errorWithMessage("unsupported LLVM instruction");
  }

  void outputLValue(Instruction *I) { Out << "  " << GetValueName(I) << " = "; }

  LLVM_ATTRIBUTE_NORETURN void errorWithMessage(const char *message);

  bool isGotoCodeNecessary(BasicBlock *From, BasicBlock *To);
  void printPHICopiesForSuccessor(BasicBlock *CurBlock, BasicBlock *Successor,
                                  unsigned Indent);
  void printBranchToBlock(BasicBlock *CurBlock, BasicBlock *SuccBlock,
                          unsigned Indent);
  void printGEPExpression(Value *Ptr, gep_type_iterator I, gep_type_iterator E);

  std::string GetElementPtrString(std::string ptr, gep_type_iterator I);
  std::string GetValueName(Value *Operand);

  friend class CWriterTestHelper;
};

} // namespace llvm_cbe
