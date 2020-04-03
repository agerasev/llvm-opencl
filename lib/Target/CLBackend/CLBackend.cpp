//===-- CLBackend.cpp - Library for converting LLVM code to C --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This library converts LLVM code to C code, compilable by GCC and other C
// compilers.
//
//===----------------------------------------------------------------------===//


#include "CLBackend.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/Signals.h"

#include "TopologicalSorter.h"
#include "StringTools.h"

#include <algorithm>
#include <cstdio>

#include <iostream>
#include <sstream>


namespace llvm_opencl {

using namespace llvm;

extern "C" void LLVMInitializeCLBackendTarget() {
  // Register the target.
  RegisterTargetMachine<CLTargetMachine> X(TheCLBackendTarget);
}

char CWriter::ID = 0;

// extra (invalid) Ops tags for tracking unary ops as a special case of the
// available binary ops
enum UnaryOps {
  BinaryNeg = Instruction::OtherOpsEnd + 1,
  BinaryNot,
};


#define cwriter_assert(expr)                                                   \
  if (!(expr)) {                                                               \
    this->errorWithMessage(#expr);                                             \
  }

static bool isEmptyType(Type *Ty) {
  if (StructType *STy = dyn_cast<StructType>(Ty))
    return STy->getNumElements() == 0 ||
           std::all_of(STy->element_begin(), STy->element_end(), isEmptyType);

  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return VTy->getNumElements() == 0 || isEmptyType(VTy->getElementType());

  if (ArrayType *ATy = dyn_cast<ArrayType>(Ty))
    return ATy->getNumElements() == 0 || isEmptyType(ATy->getElementType());

  return Ty->isVoidTy();
}

bool CWriter::isEmptyType(Type *Ty) const { return llvm_opencl::isEmptyType(Ty); }

/// isAddressExposed - Return true if the specified value's name needs to
/// have its address taken in order to get a C value of the correct type.
/// This happens for global variables, byval parameters, and direct allocas.
bool CWriter::isAddressExposed(Value *V) const {
  if (Argument *A = dyn_cast<Argument>(V))
    return ByValParams.count(A) > 0;
  else
    return isa<GlobalVariable>(V) || isDirectAlloca(V);
}

// isInlinableInst - Attempt to inline instructions into their uses to build
// trees as much as possible.  To do this, we have to consistently decide
// what is acceptable to inline, so that variable declarations don't get
// printed and an extra copy of the expr is not emitted.
bool CWriter::isInlinableInst(Instruction &I) const {
  // TODO_: For now we inline nothing, but need to consider is it reasonable
  return false;

  // Always inline cmp instructions, even if they are shared by multiple
  // expressions.  GCC generates horrible code if we don't.
  if (isa<CmpInst>(I))
    return true;

  // Must be an expression, must be used exactly once.  If it is dead, we
  // emit it inline where it would go.
  if (isEmptyType(I.getType()) || !I.hasOneUse() || I.isTerminator() ||
      isa<CallInst>(I) || isa<PHINode>(I) || isa<LoadInst>(I) ||
      isa<VAArgInst>(I) || isa<InsertElementInst>(I) || isa<InsertValueInst>(I))
    // Don't inline a load across a store or other bad things!
    return false;

  // Only inline instruction if its use is in the same BB as the inst.
  return I.getParent() == cast<Instruction>(I.user_back())->getParent();
}

// isDirectAlloca - Define fixed sized allocas in the entry block as direct
// variables which are accessed with the & operator.  This causes GCC to
// generate significantly better code than to emit alloca calls directly.
AllocaInst *CWriter::isDirectAlloca(Value *V) const {
  AllocaInst *AI = dyn_cast<AllocaInst>(V);
  if (!AI)
    return nullptr;
  if (AI->isArrayAllocation())
    return nullptr; // FIXME: we can also inline fixed size array allocas!
  if (AI->getParent() != &AI->getParent()->getParent()->getEntryBlock())
    return nullptr;
  return AI;
}

bool CWriter::runOnFunction(Function &F) {
  // Do not codegen any 'available_externally' functions at all, they have
  // definitions outside the translation unit.
  if (F.hasAvailableExternallyLinkage())
    return false;

  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();

  // Get rid of intrinsics we can't handle.
  bool Modified = lowerIntrinsics(F);

  printFunction(F);

  LI = nullptr;

  return Modified;
}

raw_ostream &CWriter::printTypeString(raw_ostream &Out, Type *Ty) {
  if (StructType *ST = dyn_cast<StructType>(Ty)) {
    cwriter_assert(!isEmptyType(ST));
    TypedefDeclTypes.insert(Ty);

    if (!ST->isLiteral() && !ST->getName().empty())
      return Out << CBEMangle(ST->getName());

    unsigned id = UnnamedStructIDs.getOrInsert(ST);
    return Out << "unnamed_" + utostr(id);
  }

  PointerType *PTy = dyn_cast<PointerType>(Ty);
  if (PTy) {
    Out << "p" << PTy->getAddressSpace();
    return printTypeString(Out, PTy->getElementType());
  }

  switch (Ty->getTypeID()) {
  case Type::VoidTyID:
    return Out << "void";
  case Type::IntegerTyID: {
    unsigned NumBits = cast<IntegerType>(Ty)->getBitWidth();
    cwriter_assert(NumBits <= 64 && "Bit widths > 64 not implemented yet");
    return Out << "i" << NumBits;
  }
  case Type::FloatTyID:
    return Out << "f32";
  case Type::DoubleTyID:
    return Out << "f64";

  case Type::VectorTyID: {
    TypedefDeclTypes.insert(Ty);
    VectorType *VTy = cast<VectorType>(Ty);
    cwriter_assert(VTy->getNumElements() != 0);
    printTypeString(Out, VTy->getElementType());
    return Out << "x" << VTy->getNumElements();
  }

  case Type::ArrayTyID: {
    TypedefDeclTypes.insert(Ty);
    ArrayType *ATy = cast<ArrayType>(Ty);
    cwriter_assert(ATy->getNumElements() != 0);
    printTypeString(Out, ATy->getElementType());
    return Out << "a" << ATy->getNumElements();
  }

  default:
    errs() << "Unknown primitive type: " << *Ty << "\n";
    errorWithMessage("unknown primitive type");
  }
}

std::string CWriter::getStructName(StructType *ST) {
  cwriter_assert(ST->getNumElements() != 0);
  if (!ST->isLiteral() && !ST->getName().empty())
    return "struct " + CBEMangle(ST->getName().str());

  unsigned id = UnnamedStructIDs.getOrInsert(ST);
  return "struct unnamed_" + utostr(id);
}

std::string
CWriter::getFunctionName(FunctionType *FT,
                         std::pair<AttributeList, CallingConv::ID> PAL) {
  unsigned id = UnnamedFunctionIDs.getOrInsert(std::make_pair(FT, PAL));
  return "fptr_" + utostr(id);
}

std::string CWriter::getArrayName(ArrayType *AT) {
  std::string astr;
  raw_string_ostream ArrayInnards(astr);
  // Arrays are wrapped in structs to allow them to have normal
  // value semantics (avoiding the array "decay").
  cwriter_assert(!isEmptyType(AT));
  printTypeName(ArrayInnards, AT->getElementType());
  return "struct array_" + utostr(AT->getNumElements()) + '_' +
         CBEMangle(ArrayInnards.str());
}

std::string CWriter::getVectorName(VectorType *VT, bool Aligned, bool isSigned) {
  std::string astr;
  raw_string_ostream VectorInnards(astr);
  // Vectors are handled like arrays
  cwriter_assert(!isEmptyType(VT));

  uint64_t n = VT->getNumElements();
  if (n != 2 && n != 3 && n != 4 && n != 8 && n != 16) {
    errs() << "Vector of length " << n << " not supported\n";
    errorWithMessage("Unsupported vector length");
  }

  printTypeName(VectorInnards, VT->getElementType(), isSigned);
  std::string t = CBEMangle(VectorInnards.str());
  if (t != "char" && t != "uchar" && t != "short" && t != "ushort" &&
      t != "int" && t != "uint" && t != "long" && t != "ulong" &&
      t != "float" && t != "double") {
    errs() << "Vector of type " << t << " not supported\n";
    errorWithMessage("Unsupported vector type");
  }
  
  return t + utostr(n);
}

std::string CWriter::getCmpPredicateName(CmpInst::Predicate P) const {
  switch (P) {
  case FCmpInst::FCMP_FALSE:
    return "0";
  case FCmpInst::FCMP_OEQ:
    return "oeq";
  case FCmpInst::FCMP_OGT:
    return "ogt";
  case FCmpInst::FCMP_OGE:
    return "oge";
  case FCmpInst::FCMP_OLT:
    return "olt";
  case FCmpInst::FCMP_OLE:
    return "ole";
  case FCmpInst::FCMP_ONE:
    return "one";
  case FCmpInst::FCMP_ORD:
    return "ord";
  case FCmpInst::FCMP_UNO:
    return "uno";
  case FCmpInst::FCMP_UEQ:
    return "ueq";
  case FCmpInst::FCMP_UGT:
    return "ugt";
  case FCmpInst::FCMP_UGE:
    return "uge";
  case FCmpInst::FCMP_ULT:
    return "ult";
  case FCmpInst::FCMP_ULE:
    return "ule";
  case FCmpInst::FCMP_UNE:
    return "une";
  case FCmpInst::FCMP_TRUE:
    return "1";
  case ICmpInst::ICMP_EQ:
    return "eq";
  case ICmpInst::ICMP_NE:
    return "ne";
  case ICmpInst::ICMP_ULE:
    return "ule";
  case ICmpInst::ICMP_SLE:
    return "sle";
  case ICmpInst::ICMP_UGE:
    return "uge";
  case ICmpInst::ICMP_SGE:
    return "sge";
  case ICmpInst::ICMP_ULT:
    return "ult";
  case ICmpInst::ICMP_SLT:
    return "slt";
  case ICmpInst::ICMP_UGT:
    return "ugt";
  case ICmpInst::ICMP_SGT:
    return "sgt";
  default:
    errs() << "Invalid icmp predicate: " << P << "\n";
    errorWithMessage("Invalid icmp predicate");
  }
}

std::string CWriter::getCmpImplem(
  CmpInst::Predicate P,
  const std::string &l, const std::string &r
) const {
  switch (P) {
  case FCmpInst::FCMP_FALSE:
    return "0";
  case ICmpInst::ICMP_EQ:
  case FCmpInst::FCMP_OEQ:
    return l + " == " + r;
  case ICmpInst::ICMP_UGT:
  case ICmpInst::ICMP_SGT:
  case FCmpInst::FCMP_OGT:
    return l + " > " + r;
  case ICmpInst::ICMP_UGE:
  case ICmpInst::ICMP_SGE:
  case FCmpInst::FCMP_OGE:
    return l + " >= " + r;
  case ICmpInst::ICMP_ULT:
  case ICmpInst::ICMP_SLT:
  case FCmpInst::FCMP_OLT:
    return l + " < " + r;
  case ICmpInst::ICMP_ULE:
  case ICmpInst::ICMP_SLE:
  case FCmpInst::FCMP_OLE:
    return l + " <= " + r;
  case FCmpInst::FCMP_ONE:
    return l + " != " + r + " && " + getCmpImplem(FCmpInst::FCMP_ORD, l, r);
  case FCmpInst::FCMP_ORD:
    return l + " == " + l + " && " + r + " == " + r;
  case FCmpInst::FCMP_UNO:
    return l + " != " + l + " || " + r + " != " + r;
  case FCmpInst::FCMP_UEQ:
    return l + " == " + r + " || " + getCmpImplem(FCmpInst::FCMP_UNO, l, r);
  case FCmpInst::FCMP_UGT:
    return l + " > " + r + " || " + getCmpImplem(FCmpInst::FCMP_UNO, l, r);
  case FCmpInst::FCMP_UGE:
    return l + " >= " + r + " || " + getCmpImplem(FCmpInst::FCMP_UNO, l, r);
  case FCmpInst::FCMP_ULT:
    return l + " < " + r + " || " + getCmpImplem(FCmpInst::FCMP_UNO, l, r);
  case FCmpInst::FCMP_ULE:
    return l + " <= " + r + " || " + getCmpImplem(FCmpInst::FCMP_UNO, l, r);
  case ICmpInst::ICMP_NE:
  case FCmpInst::FCMP_UNE:
    return l + " != " + r;
  case FCmpInst::FCMP_TRUE:
    return "1";

  default:
    errs() << "Invalid fcmp predicate: " << P << "\n";
    errorWithMessage("Invalid fcmp predicate!");
  }
}

raw_ostream &CWriter::printSimpleType(raw_ostream &Out, Type *Ty,
                                      bool isSigned) {
  cwriter_assert((Ty->isSingleValueType() || Ty->isVoidTy()) &&
                 "Invalid type for printSimpleType");
  switch (Ty->getTypeID()) {
  case Type::VoidTyID:
    return Out << "void";
  case Type::IntegerTyID: {
    unsigned NumBits = cast<IntegerType>(Ty)->getBitWidth();
    if (NumBits == 1)
      //return Out << "bool";
      // OpenCL has no support for bool vectors
      return Out << (isSigned ? "char" : "uchar");
    else if (NumBits <= 8)
      return Out << (isSigned ? "char" : "uchar");
    else if (NumBits <= 16)
      return Out << (isSigned ? "short" : "ushort");
    else if (NumBits <= 32)
      return Out << (isSigned ? "int" : "uint");
    else if (NumBits <= 64)
      return Out << (isSigned ? "long" : "ulong");
    else
      errorWithMessage("Bit widths > 64 not implemented yet");
  }
  case Type::FloatTyID:
    return Out << "float";
  case Type::DoubleTyID:
    return Out << "double";

  default:
    errs() << "Unknown primitive type: " << *Ty;
    errorWithMessage("unknown primitive type");
  }
}

void CWriter::printWithCast(
  raw_ostream &Out, Type *DstTy, bool isSigned,
  std::function<void()> print_inner, bool cond
) {
  if (!cond) {
    print_inner();
    return;
  }
  if (DstTy->isVectorTy()) {
    Out << "convert_";
    printTypeName(Out, DstTy, isSigned);
    Out << "(";
    print_inner();
    Out << ")";
  } else {
    Out << "(";
    printTypeName(Out, DstTy, isSigned);
    Out << ")(";
    print_inner();
    Out << ")";
  }
}

void CWriter::printWithCast(
  raw_ostream &Out, Type *DstTy, bool isSigned,
  const std::string &inner, bool cond
) {
  printWithCast(Out, DstTy, isSigned, [&]() { Out << inner; }, cond);
}

unsigned int CWriter::getNextPowerOf2(unsigned int width) {
  if (width <= 8) {
    return 8;
  } else if (width <= 16) {
    return 16;
  } else if (width <= 32) {
    return 32;
  } else if (width <= 64) {
    return 64;
  } else {
    errorWithMessage("Integers of size larger than 64 is not supported");
  }
}

uint64_t CWriter::getIntPadded(uint64_t value, unsigned int width) {
  unsigned int padding = getNextPowerOf2(width) - width;
  return (int64_t)(value << padding) >> padding;
}

// OpenCL cannot handle non-power-of-2 integers (except bool).
// So we need to contain such integers in larger power-of-2 types.
// In order to have proper result in cast and arithmetic operations
// we need to fill unused bits with the most significant bit of integer.
void CWriter::printPadded(
  raw_ostream &Out, Type *Ty,
  std::function<void()> print_inner, bool cond
) {
  if(!cond || !Ty->isIntOrIntVectorTy()) {
    print_inner();
    return;
  }
  IntegerType *ITy;
  if (Ty->isVectorTy()) {
    ITy = cast<IntegerType>(Ty->getVectorElementType());
  } else {
    ITy = cast<IntegerType>(Ty);
  }
  if (ITy->isPowerOf2ByteWidth()) {
    // Already in required form
    print_inner();
  } else {
    // Truncate to bit size while filling padding bits with sign bit
    unsigned int width = ITy->getBitWidth();
    unsigned int padding_width = getNextPowerOf2(width) - width;
    printWithCast(Out, Ty, false, [&]() {
      Out << "(";
      printWithCast(Out, Ty, true, [&]() {
        print_inner();
        Out << " << ";
        printWithCast(Out, ITy, false, "0x" + utohexstr(padding_width));
      });
      // The right-shift of negative value is not UB in OpenCL unlike C
      Out << " >> ";
      printWithCast(Out, ITy, true, "0x" + utohexstr(padding_width));
      Out << ")";
    });
  }
}

void CWriter::printPadded(
  raw_ostream &Out, Type *Ty,
  const std::string &inner, bool cond
) {
  printPadded(Out, Ty, [&]() { Out << inner; }, cond);
}

void CWriter::printUnpadded(
  raw_ostream &Out, Type *Ty,
  std::function<void()> print_inner, bool cond
) {
  if(!cond || !Ty->isIntOrIntVectorTy()) {
    print_inner();
    return;
  }
  IntegerType *ITy;
  if (Ty->isVectorTy()) {
    ITy = cast<IntegerType>(Ty->getVectorElementType());
  } else {
    ITy = cast<IntegerType>(Ty);
  }
  if (ITy->isPowerOf2ByteWidth()) {
    print_inner();
  } else {
    Out << "(";
    print_inner();
    Out << " & ";
    printWithCast(Out, ITy, false, "0x" + utohexstr(ITy->getBitMask()));
    Out << ")";
  }
}

void CWriter::printUnpadded(
  raw_ostream &Out, Type *Ty,
  const std::string &inner, bool cond
) {
  printUnpadded(Out, Ty, [&]() { Out << inner; }, cond);
}

// Pass the Type* and the variable name and this prints out the variable
// declaration.
raw_ostream &
CWriter::printTypeName(raw_ostream &Out, Type *Ty, bool isSigned,
                       std::pair<AttributeList, CallingConv::ID> PAL) {
  if (Ty->isSingleValueType() || Ty->isVoidTy()) {
    if (!Ty->isPointerTy() && !Ty->isVectorTy())
      return printSimpleType(Out, Ty, isSigned);
  }

  if (isEmptyType(Ty))
    return Out << "void";

  switch (Ty->getTypeID()) {
  case Type::FunctionTyID: {
    FunctionType *FTy = cast<FunctionType>(Ty);
    return Out << getFunctionName(FTy, PAL);
  }
  case Type::StructTyID: {
    TypedefDeclTypes.insert(Ty);
    return Out << getStructName(cast<StructType>(Ty));
  }

  case Type::PointerTyID: {
    Type *ElTy = Ty->getPointerElementType();
    printTypeName(Out, ElTy);
    switch (Ty->getPointerAddressSpace()) {
      case 0:
        Out << " __private";
        break;
      case 1:
        Out << " __global";
        break;
      case 2:
        Out << " __constant";
        break;
      case 3:
        Out << " __local";
        break;
      case 4:
        Out << ""; // OpenCL 2.x generic address space
        break;
      default:
      errs() << "Invalid address space " << Ty->getPointerAddressSpace() << "\n";
      errorWithMessage("Encountered Invalid Address Space");
      break;
    }
    Out << "*";
    return Out;
  }

  case Type::ArrayTyID: {
    TypedefDeclTypes.insert(Ty);
    return Out << getArrayName(cast<ArrayType>(Ty));
  }

  case Type::VectorTyID: {
    TypedefDeclTypes.insert(Ty);
    return Out << getVectorName(cast<VectorType>(Ty), true, isSigned);
  }

  default:
    errs() << "Unexpected type: " << *Ty << "\n";
    errorWithMessage("unexpected type");
  }
}

raw_ostream &CWriter::printStructDeclaration(raw_ostream &Out,
                                             StructType *STy) {
  Out << getStructName(STy) << " {\n";
  unsigned Idx = 0;
  for (StructType::element_iterator I = STy->element_begin(),
                                    E = STy->element_end();
       I != E; ++I, Idx++) {
    Out << "  ";
    bool empty = isEmptyType(*I);
    if (empty)
      Out << "/* "; // skip zero-sized types
    printTypeName(Out, *I) << " f" << utostr(Idx);
    if (empty)
      Out << " */"; // skip zero-sized types
    else
      Out << ";\n";
  }
  Out << '}';
  if (STy->isPacked())
    Out << " __attribute__ ((packed))";
  Out << ";\n";
  return Out;
}

raw_ostream &CWriter::printFunctionDeclaration(
    raw_ostream &Out, FunctionType *Ty,
    std::pair<AttributeList, CallingConv::ID> PAL) {
  Out << "typedef ";
  printFunctionProto(Out, Ty, PAL, getFunctionName(Ty, PAL), nullptr);
  return Out << ";\n";
}

raw_ostream &
CWriter::printFunctionProto(raw_ostream &Out, FunctionType *Ty,
                     std::pair<AttributeList, CallingConv::ID> Attrs,
                     const std::string &Name,
                     iterator_range<Function::arg_iterator> *ArgList) {
  // Cache
  int Idx = 0;
  Function::arg_iterator ArgName = Function::arg_iterator();
  if (ArgList) {
    ArgName = ArgList->begin();
  }
  return printFunctionProto(Out, Ty, Attrs, Name, ArgList, [&](int i) {
    if (ArgList) {
      if (i < Idx) {
        Idx = 0;
        ArgName = ArgList->begin();
      }
      for (; Idx < i; ++Idx) {
        ++ArgName;
      }
    }
    return GetValueName(ArgName);
  });
}

raw_ostream &
CWriter::printFunctionProto(raw_ostream &Out, FunctionType *FTy,
                            std::pair<AttributeList, CallingConv::ID> Attrs,
                            const std::string &Name,
                            iterator_range<Function::arg_iterator> *ArgList,
                            std::function<std::string(int)> GetArgName) {
  AttributeList &PAL = Attrs.first;

  // Should this function actually return a struct by-value?
  bool isStructReturn = PAL.hasAttribute(1, Attribute::StructRet);
  // Get the return type for the function.
  Type *RetTy;
  if (!isStructReturn)
    RetTy = FTy->getReturnType();
  else {
    // If this is a struct-return function, print the struct-return type.
    RetTy = cast<PointerType>(FTy->getParamType(0))->getElementType();
  }
  printTypeName(Out, RetTy);

  switch (Attrs.second) {
  case CallingConv::C:
    break;
  case CallingConv::SPIR_FUNC:
    break;
  case CallingConv::SPIR_KERNEL:
    Out << " __kernel";
    break;
  default:
    errs() << "Unhandled calling convention " << Attrs.second << "\n";
    errorWithMessage("Encountered Unhandled Calling Convention");
    break;
  }
  Out << ' ' << Name << '(';

  unsigned Idx = 1;
  bool PrintedArg = false;
  FunctionType::param_iterator I = FTy->param_begin(), E = FTy->param_end();

  // If this is a struct-return function, don't print the hidden
  // struct-return argument.
  if (isStructReturn) {
    cwriter_assert(I != E && "Invalid struct return function!");
    ++I;
    ++Idx;
  }

  for (; I != E; ++I) {
    Type *ArgTy = *I;
    bool isByVal = PAL.hasAttribute(Idx, Attribute::ByVal);
    if (isByVal) {
      cwriter_assert(ArgTy->isPointerTy());
      ArgTy = cast<PointerType>(ArgTy)->getElementType();
    }
    if (PrintedArg)
      Out << ", ";
    printTypeName(Out, ArgTy);
    PrintedArg = true;
    if (ArgList) {
      Out << ' ' << GetArgName(Idx - 1);
      if (isByVal) {
        Out << "_val";
      }
    }
    ++Idx;
  }

  if (FTy->isVarArg()) {
    if (!PrintedArg) {
      Out << "int"; // dummy argument for empty vaarg functs
      if (ArgList)
        Out << " vararg_dummy_arg";
    }
    Out << ", ...";
  } else if (!PrintedArg) {
    Out << "void";
  }
  Out << ")";
  return Out;
}

raw_ostream &CWriter::printArrayDeclaration(raw_ostream &Out, ArrayType *ATy) {
  cwriter_assert(!isEmptyType(ATy));
  // Arrays are wrapped in structs to allow them to have normal
  // value semantics (avoiding the array "decay").
  Out << getArrayName(ATy) << " {\n  ";
  printTypeName(Out, ATy->getElementType());
  Out << " a[" << utostr(ATy->getNumElements()) << "];\n};\n";
  return Out;
}

raw_ostream &CWriter::printVectorDeclaration(raw_ostream &Out,
                                             VectorType *VTy) {
  cwriter_assert(!isEmptyType(VTy));
  // Vectors are builtin in OpenCL
  return Out;
}


void CWriter::printConstantArray(ConstantArray *CPA,
                                 enum OperandContext Context) {
  printConstant(cast<Constant>(CPA->getOperand(0)), Context);
  for (unsigned i = 1, e = CPA->getNumOperands(); i != e; ++i) {
    Out << ", ";
    printConstant(cast<Constant>(CPA->getOperand(i)), Context);
  }
}

void CWriter::printConstantVector(ConstantVector *CP,
                                  enum OperandContext Context) {
  printConstant(cast<Constant>(CP->getOperand(0)), Context);
  for (unsigned i = 1, e = CP->getNumOperands(); i != e; ++i) {
    Out << ", ";
    printConstant(cast<Constant>(CP->getOperand(i)), Context);
  }
}

void CWriter::printConstantDataSequential(ConstantDataSequential *CDS,
                                          enum OperandContext Context) {
  printConstant(CDS->getElementAsConstant(0), Context);
  for (unsigned i = 1, e = CDS->getNumElements(); i != e; ++i) {
    Out << ", ";
    printConstant(CDS->getElementAsConstant(i), Context);
  }
}

bool CWriter::printConstantString(Constant *C, enum OperandContext Context) {
  // As a special case, print the array as a string if it is an array of
  // ubytes or an array of sbytes with positive values.
  ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(C);
  if (!CDS || !CDS->isCString())
    return false;
  if (Context != ContextStatic)
    return false; // TODO

  Out << "{ \"";
  // Keep track of whether the last number was a hexadecimal escape.
  bool LastWasHex = false;

  StringRef Bytes = CDS->getAsString();

  // Do not include the last character, which we know is null
  for (unsigned i = 0, e = Bytes.size() - 1; i < e; ++i) {
    unsigned char C = Bytes[i];

    // Print it out literally if it is a printable character.  The only thing
    // to be careful about is when the last letter output was a hex escape
    // code, in which case we have to be careful not to print out hex digits
    // explicitly (the C compiler thinks it is a continuation of the previous
    // character, sheesh...)
    if (isprint(C) && (!LastWasHex || !isxdigit(C))) {
      LastWasHex = false;
      if (C == '"' || C == '\\')
        Out << "\\" << (char)C;
      else
        Out << (char)C;
    } else {
      LastWasHex = false;
      switch (C) {
      case '\n':
        Out << "\\n";
        break;
      case '\t':
        Out << "\\t";
        break;
      case '\r':
        Out << "\\r";
        break;
      case '\v':
        Out << "\\v";
        break;
      case '\a':
        Out << "\\a";
        break;
      case '\"':
        Out << "\\\"";
        break;
      case '\'':
        Out << "\\\'";
        break;
      default:
        Out << "\\x";
        Out << (char)((C / 16 < 10) ? (C / 16 + '0') : (C / 16 - 10 + 'A'));
        Out << (char)(((C & 15) < 10) ? ((C & 15) + '0')
                                      : ((C & 15) - 10 + 'A'));
        LastWasHex = true;
        break;
      }
    }
  }
  Out << "\" }";
  return true;
}

raw_ostream &CWriter::printVectorComponent(raw_ostream &Out, uint64_t i) {
  static const char comps[17] = "0123456789ABCDEF";
  if (i < 16) {
    Out << "s" << comps[i];
  } else {
    errs() << "Vector component index is "  << i << " but it cannot be greater than 15\n";
    errorWithMessage("Vector component error");
  }
  return Out;
}

raw_ostream &CWriter::printVectorShuffled(
  raw_ostream &Out, const std::vector<uint64_t> &mask
) {
  static const char comps[17] = "0123456789ABCDEF";
  size_t s = mask.size();
  if (s != 2 && s != 3 && s != 4 && s != 8 && s != 16) {
    errs() << "Shuffled vector size is "  << s << " but it can only be 2, 3, 4, 8 or 16\n";
    errorWithMessage("Shuffled vector size error");
  }
  Out << "s";
  for (uint64_t i : mask) {
    if (i < 16) {
      Out << comps[i];
    } else {
      errs() << "Vector component index is "  << i << " but it cannot be greater than 15\n";
      errorWithMessage("Vector component error");
    }
  }
  return Out;
}

// TODO copied from CppBackend, new code should use raw_ostream
static inline std::string ftostr(const APFloat &V) {
  std::string Buf;
  if (&V.getSemantics() == &APFloat::IEEEdouble()) {
    raw_string_ostream(Buf) << V.convertToDouble();
    return Buf;
  } else if (&V.getSemantics() == &APFloat::IEEEsingle()) {
    raw_string_ostream(Buf) << (double)V.convertToFloat();
    return Buf;
  }
  return "<unknown format in ftostr>"; // error
}
/// Print out the casting for a cast operation. This does the double casting
/// necessary for conversion to the destination type, if necessary.
/// @brief Print a cast
void CWriter::printCast(unsigned opc, Type *SrcTy, Type *DstTy) {
  // Print the destination type cast
  switch (opc) {
  case Instruction::UIToFP:
  case Instruction::SIToFP:
  case Instruction::IntToPtr:
  case Instruction::Trunc:
  case Instruction::BitCast:
  case Instruction::FPExt:
  case Instruction::FPTrunc: // For these the DstTy sign doesn't matter
    Out << '(';
    printTypeName(Out, DstTy);
    Out << ')';
    break;
  case Instruction::ZExt:
  case Instruction::PtrToInt:
  case Instruction::FPToUI: // For these, make sure we get an unsigned dest
    Out << '(';
    printSimpleType(Out, DstTy, false);
    Out << ')';
    break;
  case Instruction::SExt:
  case Instruction::FPToSI: // For these, make sure we get a signed dest
    Out << '(';
    printSimpleType(Out, DstTy, true);
    Out << ')';
    break;
  default:
    errorWithMessage("Invalid cast opcode");
  }

  // Print the source type cast
  switch (opc) {
  case Instruction::UIToFP:
  case Instruction::ZExt:
    Out << '(';
    printSimpleType(Out, SrcTy, false);
    Out << ')';
    break;
  case Instruction::SIToFP:
  case Instruction::SExt:
    Out << '(';
    printSimpleType(Out, SrcTy, true);
    Out << ')';
    break;
  case Instruction::IntToPtr:
  case Instruction::PtrToInt:
    // Avoid "cast to pointer from integer of different size" warnings
    Out << "(uintptr_t)";
    break;
  case Instruction::Trunc:
  case Instruction::BitCast:
  case Instruction::FPExt:
  case Instruction::FPTrunc:
  case Instruction::FPToSI:
  case Instruction::FPToUI:
    break; // These don't need a source cast.
  default:
    errorWithMessage("Invalid cast opcode");
  }
}

// printConstant - The LLVM Constant to C Constant converter.
void CWriter::printConstant(Constant *CPV, enum OperandContext Context) {
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(CPV)) {
    errorWithMessage("Constant expressions is not supported");
  } else if (isa<UndefValue>(CPV) && CPV->getType()->isSingleValueType()) {
    if (CPV->getType()->isVectorTy()) {
      // TODO_: Check this branch
      if (Context == ContextStatic) {
        Out << "{}";
        return;
      }
      VectorType *VT = cast<VectorType>(CPV->getType());
      cwriter_assert(!isEmptyType(VT));
      CtorDeclTypes.insert(VT);
      Out << "/*undef*/llvm_ctor_";
      printTypeString(Out, VT);
      Out << "(";
      Constant *Zero = Constant::getNullValue(VT->getElementType());
      unsigned NumElts = VT->getNumElements();
      for (unsigned i = 0; i != NumElts; ++i) {
        if (i)
          Out << ", ";
        printConstant(Zero);
      }
      Out << ")";

    } else {
      Constant *Zero = Constant::getNullValue(CPV->getType());
      Out << "/*UNDEF*/";
      return printConstant(Zero, Context);
    }
    return;
  }

  if (ConstantInt *CI = dyn_cast<ConstantInt>(CPV)) {
    Type *Ty = CI->getType();
    unsigned ActiveBits = CI->getValue().getMinSignedBits();
    if (Ty == Type::getInt1Ty(CPV->getContext())) {
      Out << (CI->getZExtValue() ? "(uchar)0xFF" : "(uchar)0");
    } else if (Context != ContextNormal && ActiveBits < 64 &&
               Ty->getPrimitiveSizeInBits() < 64 &&
               ActiveBits < Ty->getPrimitiveSizeInBits()) {
      Out << CI->getSExtValue(); // most likely a shorter representation
    } else if (Ty->getPrimitiveSizeInBits() < 32 && Context == ContextNormal) {
      Out << "((";
      printSimpleType(Out, Ty, false) << ')';
      if (CI->isMinValue(true))
        Out << CI->getZExtValue() << 'u';
      else
        Out << CI->getSExtValue();
      Out << ')';
    } else if (Ty->getPrimitiveSizeInBits() <= 32) {
      Out << CI->getZExtValue() << 'u';
    } else if (Ty->getPrimitiveSizeInBits() <= 64) {
      Out << CI->getZExtValue() << "ul";
    } else {
      errorWithMessage("Integers larger than 64 bits are not supported");
    }
    return;
  }

  ConstantFP *FPC = dyn_cast<ConstantFP>(CPV);
  if (FPC) {
    printFPConstantValue(Out, FPC, Context);
    return;
  }
  
  switch (CPV->getType()->getTypeID()) {
  case Type::IntegerTyID:
  case Type::FloatTyID:
  case Type::DoubleTyID:
    // Already handled
    break;
  case Type::ArrayTyID: {
    if (printConstantString(CPV, Context))
      break;
    ArrayType *AT = cast<ArrayType>(CPV->getType());
    cwriter_assert(AT->getNumElements() != 0 && !isEmptyType(AT));
    if (Context != ContextStatic) {
      CtorDeclTypes.insert(AT);
      Out << "llvm_ctor_";
      printTypeString(Out, AT);
      Out << "(";
      Context = ContextNormal;
    } else {
      Out << "{ { "; // Arrays are wrapped in struct types.
    }
    if (ConstantArray *CA = dyn_cast<ConstantArray>(CPV)) {
      printConstantArray(CA, Context);
    } else if (ConstantDataSequential *CDS =
                   dyn_cast<ConstantDataSequential>(CPV)) {
      printConstantDataSequential(CDS, Context);
    } else {
      cwriter_assert(isa<ConstantAggregateZero>(CPV) || isa<UndefValue>(CPV));
      Constant *CZ = Constant::getNullValue(AT->getElementType());
      printConstant(CZ, Context);
      for (unsigned i = 1, e = AT->getNumElements(); i != e; ++i) {
        Out << ", ";
        printConstant(CZ, Context);
      }
    }
    Out << (Context == ContextStatic
                ? " } }"
                : ")"); // Arrays are wrapped in struct types.
    break;
  }
  case Type::VectorTyID: {
    VectorType *VT = cast<VectorType>(CPV->getType());
    cwriter_assert(VT->getNumElements() != 0 && !isEmptyType(VT));
    if (Context != ContextStatic) {
      CtorDeclTypes.insert(VT);
      Out << "llvm_ctor_";
      printTypeString(Out, VT);
      Out << "(";
      Context = ContextNormal;
    } else {
      Out << "{ ";
    }
    if (ConstantVector *CV = dyn_cast<ConstantVector>(CPV)) {
      printConstantVector(CV, Context);
    } else if (ConstantDataSequential *CDS =
                   dyn_cast<ConstantDataSequential>(CPV)) {
      printConstantDataSequential(CDS, Context);
    } else {
      cwriter_assert(isa<ConstantAggregateZero>(CPV) || isa<UndefValue>(CPV));
      Constant *CZ = Constant::getNullValue(VT->getElementType());
      printConstant(CZ, Context);
      for (unsigned i = 1, e = VT->getNumElements(); i != e; ++i) {
        Out << ", ";
        printConstant(CZ, Context);
      }
    }
    Out << (Context == ContextStatic ? " }" : ")");
    break;
  }
  case Type::StructTyID: {
    StructType *ST = cast<StructType>(CPV->getType());
    cwriter_assert(!isEmptyType(ST));
    if (Context != ContextStatic) {
      CtorDeclTypes.insert(ST);
      Out << "llvm_ctor_";
      printTypeString(Out, ST);
      Out << "(";
      Context = ContextNormal;
    } else {
      Out << "{ ";
    }

    if (isa<ConstantAggregateZero>(CPV) || isa<UndefValue>(CPV)) {
      bool printed = false;
      for (unsigned i = 0, e = ST->getNumElements(); i != e; ++i) {
        Type *ElTy = ST->getElementType(i);
        if (isEmptyType(ElTy))
          continue;
        if (printed)
          Out << ", ";
        printConstant(Constant::getNullValue(ElTy), Context);
        printed = true;
      }
      cwriter_assert(printed);
    } else {
      bool printed = false;
      for (unsigned i = 0, e = CPV->getNumOperands(); i != e; ++i) {
        Constant *C = cast<Constant>(CPV->getOperand(i));
        if (isEmptyType(C->getType()))
          continue;
        if (printed)
          Out << ", ";
        printConstant(C, Context);
        printed = true;
      }
      cwriter_assert(printed);
    }
    Out << (Context == ContextStatic ? " }" : ")");
    break;
  }
  case Type::PointerTyID: {
    if (isa<ConstantPointerNull>(CPV)) {
      Out << "(";
      printTypeName(Out, CPV->getType());
      Out << ")0";
    } else {
      errorWithMessage("Non-null pointer constant is not supported");
    }
    break;
  }
  default:
    errs() << "This constant type is not supported: " << *CPV << "\n";
    errorWithMessage("This constant type is not supported");
  }
}

//  Print a constant assuming that it is the operand for a given Opcode. The
//  opcodes that care about sign need to cast their operands to the expected
//  type before the operation proceeds. This function does the casting.
void CWriter::printConstantWithCast(Constant *CPV, unsigned Opcode) {

  // Extract the operand's type, we'll need it.
  Type *OpTy = CPV->getType();
  // TODO: VectorType are valid here, but not supported
  if (!OpTy->isIntegerTy() && !OpTy->isFloatingPointTy()) {
    errs() << "Unsupported 'constant with cast' type " << *OpTy
           << " in: " << *CPV << "\n"
           << "\n";
    errorWithMessage("Unsupported 'constant with cast' type");
  }

  // Indicate whether to do the cast or not.
  bool shouldCast;
  bool typeIsSigned;
  opcodeNeedsCast(Opcode, shouldCast, typeIsSigned);

  // Write out the casted constant if we should, otherwise just write the
  // operand.
  if (shouldCast) {
    Out << "((";
    printSimpleType(Out, OpTy, typeIsSigned);
    Out << ")";
    printConstant(CPV);
    Out << ")";
  } else
    printConstant(CPV);
}

std::string CWriter::GetValueName(Value *Operand) {

  // Resolve potential alias.
  if (GlobalAlias *GA = dyn_cast<GlobalAlias>(Operand)) {
    Operand = GA->getAliasee();
  }

  std::string Name = Operand->getName();
  if (Name.empty()) { // Assign unique names to local temporaries.
    unsigned No = AnonValueNumbers.getOrInsert(Operand);
    Name = "tmp_" + utostr(No);
  }

  // Mangle globals with the standard mangler interface for LLC compatibility.
  if (isa<GlobalValue>(Operand)) {
    switch (builtins.find(Name.data(), nullptr)) {
    case -1:
      errorWithMessage("Built-in check unexpected error");
    case 0:
      return CBEMangle(Name);
    case 1:
      return "builtin_" + CBEMangle(Name);
    }
  }

  return CBEMangle(Name);
}

/// writeInstComputationInline - Emit the computation for the specified
/// instruction inline, with no destination provided.
void CWriter::writeInstComputationInline(Instruction &I) {
  visit(&I);
}

void CWriter::writeOperandInternal(Value *Operand,
                                   enum OperandContext Context) {
  if (Instruction *I = dyn_cast<Instruction>(Operand))
    // Should we inline this instruction to build a tree?
    if (isInlinableInst(*I) && !isDirectAlloca(I)) {
      Out << '(';
      writeInstComputationInline(*I);
      Out << ')';
      return;
    }

  Constant *CPV = dyn_cast<Constant>(Operand);

  if (CPV && !isa<GlobalValue>(CPV))
    printConstant(CPV, Context);
  else
    Out << GetValueName(Operand);
}

void CWriter::writeOperand(Value *Operand, enum OperandContext Context) {
  bool isAddressImplicit = isAddressExposed(Operand);
  if (isAddressImplicit)
    Out << "(&"; // Global variables are referenced as their addresses by llvm

  writeOperandInternal(Operand, Context);

  if (isAddressImplicit)
    Out << ')';
}

/// writeOperandDeref - Print the result of dereferencing the specified
/// operand with '*'.  This is equivalent to printing '*' then using
/// writeOperand, but avoids excess syntax in some cases.
void CWriter::writeOperandDeref(Value *Operand) {
  if (isAddressExposed(Operand)) {
    // Already something with an address exposed.
    writeOperandInternal(Operand);
  } else {
    Out << "*(";
    writeOperand(Operand);
    Out << ")";
  }
}

// Some instructions need to have their result value casted back to the
// original types because their operands were casted to the expected type.
// This function takes care of detecting that case and printing the cast
// for the Instruction.
bool CWriter::writeInstructionCast(Instruction &I) {
  Type *Ty = I.getOperand(0)->getType();
  switch (I.getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
    // We need to cast integer arithmetic so that it is always performed
    // as unsigned, to avoid undefined behavior on overflow.
  case Instruction::LShr:
  case Instruction::URem:
  case Instruction::UDiv:
    Out << "((";
    printSimpleType(Out, Ty, false);
    Out << ")(";
    return true;
  case Instruction::AShr:
  case Instruction::SRem:
  case Instruction::SDiv:
    Out << "((";
    printSimpleType(Out, Ty, true);
    Out << ")(";
    return true;
  default:
    break;
  }
  return false;
}

// Write the operand with a cast to another type based on the Opcode being used.
// This will be used in cases where an instruction has specific type
// requirements (usually signedness) for its operands.
void CWriter::opcodeNeedsCast(
    unsigned Opcode,
    // Indicate whether to do the cast or not.
    bool &shouldCast,
    // Indicate whether the cast should be to a signed type or not.
    bool &castIsSigned) {

  // Based on the Opcode for which this Operand is being written, determine
  // the new type to which the operand should be casted by setting the value
  // of OpTy. If we change OpTy, also set shouldCast to true.
  switch (Opcode) {
  default:
    // for most instructions, it doesn't matter
    shouldCast = false;
    castIsSigned = false;
    break;
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
    // We need to cast integer arithmetic so that it is always performed
    // as unsigned, to avoid undefined behavior on overflow.
  case Instruction::LShr:
  case Instruction::UDiv:
  case Instruction::URem: // Cast to unsigned first
    shouldCast = true;
    castIsSigned = false;
    break;
  case Instruction::GetElementPtr:
  case Instruction::AShr:
  case Instruction::SDiv:
  case Instruction::SRem: // Cast to signed first
    shouldCast = true;
    castIsSigned = true;
    break;
  }
}
// TODO_: Remove?
void CWriter::writeOperandWithCast(Value *Operand, unsigned Opcode) {
  // Write out the casted operand if we should, otherwise just write the
  // operand.

  // Extract the operand's type, we'll need it.
  bool shouldCast;
  bool castIsSigned;
  opcodeNeedsCast(Opcode, shouldCast, castIsSigned);

  Type *OpTy = Operand->getType();
  if (shouldCast) {
    Out << "((";
    printSimpleType(Out, OpTy, castIsSigned);
    Out << ")";
    writeOperand(Operand);
    Out << ")";
  } else
    writeOperand(Operand);
}

// Write the operand with a cast to another type based on the icmp predicate
// being used.
// TODO_: Remove?
void CWriter::writeOperandWithCast(Value *Operand, ICmpInst &Cmp) {
  // This has to do a cast to ensure the operand has the right signedness.
  // Also, if the operand is a pointer, we make sure to cast to an integer when
  // doing the comparison both for signedness and so that the C compiler doesn't
  // optimize things like "p < NULL" to false (p may contain an integer value
  // f.e.).
  bool shouldCast = Cmp.isRelational();

  // Write out the casted operand if we should, otherwise just write the
  // operand.
  if (!shouldCast) {
    writeOperand(Operand);
    return;
  }

  // Should this be a signed comparison?  If so, convert to signed.
  bool castIsSigned = Cmp.isSigned();

  // If the operand was a pointer, convert to a large integer type.
  Type *OpTy = Operand->getType();
  if (OpTy->isPointerTy())
    OpTy = TD->getIntPtrType(Operand->getContext());

  Out << "((";
  printSimpleType(Out, OpTy, castIsSigned);
  Out << ")";
  writeOperand(Operand);
  Out << ")";
}

/// FindStaticTors - Given a static ctor/dtor list, unpack its contents into
/// the StaticTors set.
static void FindStaticTors(GlobalVariable *GV,
                           std::set<Function *> &StaticTors) {
  ConstantArray *InitList = dyn_cast<ConstantArray>(GV->getInitializer());
  if (!InitList)
    return;

  for (unsigned i = 0, e = InitList->getNumOperands(); i != e; ++i)
    if (ConstantStruct *CS =
            dyn_cast<ConstantStruct>(InitList->getOperand(i))) {
      if (CS->getNumOperands() != 2)
        return; // Not array of 2-element structs.

      if (CS->getOperand(1)->isNullValue())
        return; // Found a null terminator, exit printing.
      Constant *FP = CS->getOperand(1);
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(FP))
        if (CE->isCast())
          FP = CE->getOperand(0);
      if (Function *F = dyn_cast<Function>(FP))
        StaticTors.insert(F);
    }
}

enum SpecialGlobalClass {
  NotSpecial = 0,
  GlobalCtors,
  GlobalDtors,
  NotPrinted
};

/// getGlobalVariableClass - If this is a global that is specially recognized
/// by LLVM, return a code that indicates how we should handle it.
static SpecialGlobalClass getGlobalVariableClass(GlobalVariable *GV) {
  // If this is a global ctors/dtors list, handle it now.
  if (GV->hasAppendingLinkage() && GV->use_empty()) {
    if (GV->getName() == "llvm.global_ctors")
      return GlobalCtors;
    else if (GV->getName() == "llvm.global_dtors")
      return GlobalDtors;
  }

  // Otherwise, if it is other metadata, don't print it.  This catches things
  // like debug information.
  if (StringRef(GV->getSection()) == "llvm.metadata")
    return NotPrinted;

  return NotSpecial;
}

bool CWriter::doInitialization(Module &M) {
  TheModule = &M;

  TD = new DataLayout(&M);
  IL = new IntrinsicLowering(*TD);
  //IL->AddPrototypes(M);

#if 0
  std::string Triple = TheModule->getTargetTriple();
  if (Triple.empty())
    Triple = llvm::sys::getDefaultTargetTriple();

  std::string E;
  if (const Target *Match = TargetRegistry::lookupTarget(Triple, E))
    TAsm = Match->createMCAsmInfo(Triple);
#endif
  TAsm = new CBEMCAsmInfo();
  MRI = new MCRegisterInfo();
  TCtx = new MCContext(TAsm, MRI, nullptr);
  return false;
}

bool CWriter::doFinalization(Module &M) {
  // Output all code to the file
  std::string methods = Out.str();
  _Out.clear();
  generateHeader(M);
  std::string header = OutHeaders.str() + Out.str();
  _Out.clear();
  _OutHeaders.clear();
  FileOut << header << methods;

  // Free memory...

  delete IL;
  IL = nullptr;

  delete TD;
  TD = nullptr;

  delete TCtx;
  TCtx = nullptr;

  delete TAsm;
  TAsm = nullptr;

  delete MRI;
  MRI = nullptr;

  delete MOFI;
  MOFI = nullptr;

  ByValParams.clear();
  AnonValueNumbers.clear();
  UnnamedStructIDs.clear();
  UnnamedFunctionIDs.clear();
  TypedefDeclTypes.clear();
  SelectDeclTypes.clear();
  CmpDeclTypes.clear();
  CastOpDeclTypes.clear();
  InlineOpDeclTypes.clear();
  CtorDeclTypes.clear();
  prototypesToGen.clear();

  return true; // may have lowered an IntrinsicCall
}



void CWriter::generateHeader(Module &M) {
  // Keep track of which functions are static ctors/dtors so they can have
  // an attribute added to their prototypes.
  std::set<Function *> StaticCtors, StaticDtors;
  for (Module::global_iterator I = M.global_begin(), E = M.global_end(); I != E;
       ++I) {
    switch (getGlobalVariableClass(&*I)) {
    default:
      break;
    case GlobalCtors:
      FindStaticTors(&*I, StaticCtors);
      break;
    case GlobalDtors:
      FindStaticTors(&*I, StaticDtors);
      break;
    }
  }

  // collect any remaining types
  raw_null_ostream NullOut;
  for (Module::global_iterator I = M.global_begin(), E = M.global_end(); I != E;
       ++I) {
    // Ignore special globals, such as debug info.
    if (getGlobalVariableClass(&*I))
      continue;
    printTypeName(NullOut, I->getType()->getElementType(), false);
  }
  printModuleTypes(Out);

  // Function declarations and wrappers for OpenCL built-ins
  Out << "\n/* Function Declarations and wrappers for OpenCL built-ins */\n";

  // Store the intrinsics which will be declared/defined below.
  SmallVector<Function *, 16> intrinsicsToDefine;

  // TODO_: Find out why non-intrinsic functions like `memset` are printed.
  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I) {
    // Don't print declarations for intrinsic functions.
    // Store the used intrinsics, which need to be explicitly defined.
    if (I->isIntrinsic()) {
      if (intrinsics.hasImpl(I->getIntrinsicID())) {
        intrinsicsToDefine.push_back(&*I);
      }
      continue;
    }

    // Skip OpenCL built-in functions
    Func func;
    switch (builtins.find(I->getName().data(), &func)) {
    case -1:
      errorWithMessage("Built-in check unexpected error");
      break;
    case 1: {
      // Is opencl built-in
      // TODO_: Handle more than 26 arguments
      auto GetArgName = [](int i) {
        return std::string() + (char)('a' + i);
      };
      auto GetTypeName = [this](Type *Ty) {
        std::string _out;
        raw_string_ostream out(_out);
        printTypeName(out, Ty, false);
        out.str();
        return _out;
      };

      Out << "// " << func.to_string() << "\n";
      Out << "static ";
      
      iterator_range<Function::arg_iterator> args = I->args();
      printFunctionProto(Out, I->getFunctionType(),
                        std::make_pair(I->getAttributes(), I->getCallingConv()),
                        GetValueName(&*I), &args, GetArgName);
      Out << " {\n";
      cwriter_assert(builtins.printDefinition(Out, func, &*I, GetArgName, GetTypeName));
      Out << "}\n";
      break;
    }
    case 0:
      // Is not opencl built-in
      if (I->hasLocalLinkage())
        Out << "static ";
      printFunctionProto(Out, &*I);
      Out << ";\n";
      break;
    }

    Out << "\n";
  }

  // Output the global variable definitions and contents...
  if (!M.global_empty()) {
    Out << "\n\n/* Global Variable Definitions and Initialization */\n";
    for (Module::global_iterator I = M.global_begin(), E = M.global_end();
         I != E; ++I) {
      declareOneGlobalVariable(&*I);
    }
  }

  // Alias declarations...
  if (!M.alias_empty()) {
    Out << "\n/* External Alias Declarations */\n";
    for (Module::alias_iterator I = M.alias_begin(), E = M.alias_end(); I != E;
         ++I) {
      cwriter_assert(!I->isDeclaration() &&
                     !isEmptyType(I->getType()->getPointerElementType()));
      if (I->hasLocalLinkage())
        continue; // Internal Global

      Type *ElTy = I->getType()->getElementType();
      unsigned Alignment = I->getAlignment();
      bool IsOveraligned =
          Alignment && Alignment > TD->getABITypeAlignment(ElTy);

      // GetValueName would resolve the alias, which is not what we want,
      // so use getName directly instead (assuming that the Alias has a name...)
      printTypeName(Out, ElTy, false) << " *" << I->getName();
      if (IsOveraligned)
        Out << " __attribute__((aligned(" << Alignment << ")))";

      Out << " = ";
      writeOperand(I->getAliasee(), ContextStatic);
      Out << ";\n";
    }
  }

  Out << "\n\n/* LLVM Intrinsic Builtin Function Bodies */\n";

  // Loop over all select operations
  for (std::set<std::pair<Type *, Type *>>::iterator it = SelectDeclTypes.begin(),
                                  end = SelectDeclTypes.end();
       it != end; ++it) {
    // static Rty llvm_select_u8x4(<bool x 4> condition, <u8 x 4>
    // iftrue, <u8 x 4> ifnot) {
    //   return convert_<u8 x 4>(condition) ? iftrue : ifnot;
    // }
    Type *CTy = it->first;
    Type *Ty = it->second;
    Out << "static ";
    printTypeName(Out, Ty, false);
    Out << " llvm_select_";
    printTypeString(Out, CTy);
    Out << "_";
    printTypeString(Out, Ty);
    Out << "(";
    printTypeName(Out, CTy);
    Out << " condition, ";
    printTypeName(Out, Ty, false);
    Out << " iftrue, ";
    printTypeName(Out, Ty, false);
    Out << " ifnot) {\n  ";


    Out << "  return ";
    VectorType *VTy = dyn_cast<VectorType>(CTy);
    if (VTy) {
      cwriter_assert(isa<VectorType>(Ty))
      Type *ElTy = Ty->getVectorElementType();
      Out << "convert_";
      switch (ElTy->getTypeID()) {
      case Type::IntegerTyID:
        printTypeName(Out, ElTy, true);
        break;
      case Type::FloatTyID:
        Out << "int";
        break;
      case Type::DoubleTyID:
        Out << "long";
        break;
      default:
        errs() << "Unknown vector element type: ";
        printTypeName(errs(), ElTy, true);
        errs() << "\n";
        errorWithMessage("Unknown vector element type");
      }
      Out << VTy->getNumElements() << "(";
      printWithCast(Out, CTy, true, "condition");
      Out << ")";
    } else {
      Out << "condition";
    }
    Out << " ? iftrue : ifnot;\n";
    Out << "}\n";
  }

  // Loop over all compare operations
  for (std::set<std::pair<CmpInst::Predicate, Type *>>::iterator
           it = CmpDeclTypes.begin(),
           end = CmpDeclTypes.end();
       it != end; ++it) {
    // static <bool x 4> llvm_icmp_ge_u8x4(<u8 x 4> l, <u8 x 4> r) {
    //   return l >= r;
    // }
    CmpInst::Predicate Pred = it->first;
    bool isSigned = CmpInst::isSigned(Pred);

    Type *Ty = it->second;
    VectorType *VTy = dyn_cast<VectorType>(Ty);
    Type *RTy;
    if (VTy) {
      // Vector type
      RTy = VectorType::get(
        Type::getInt1Ty(VTy->getContext()),
        VTy->getVectorNumElements()
      );
    } else {
      // Scalar type
      RTy = Type::getInt1Ty(Ty->getContext());
    }

    Out << "static ";
    printTypeName(Out, RTy);
    if (CmpInst::isFPPredicate(Pred))
      Out << " llvm_fcmp_";
    else
      Out << " llvm_icmp_";
    Out << getCmpPredicateName(Pred) << "_";
    printTypeString(Out, Ty);
    Out << "(";
    printTypeName(Out, Ty);
    Out << " l, ";
    printTypeName(Out, Ty);
    Out << " r) {\n";

    std::string args[2] = { "l", "r" };
    if (Ty->isIntOrIntVectorTy() && isSigned) {
      for (int i = 0; i < 2; ++i) {
        std::string tmp;
        raw_string_ostream os(tmp);
        printWithCast(os, Ty, isSigned, args[i]);
        args[i] = os.str();
      }
    }
    Out << "  return ";
    printWithCast(Out, RTy, false, [&](){
      std::string cmp_res = getCmpImplem(Pred, args[0], args[1]);
      if (Ty->isVectorTy()) {
        printWithCast(Out, RTy, true, cmp_res);
      } else {
        Out << "(" << cmp_res << ") ? 0xFF : 0";
      }
    });
    Out << ";\n}\n";
  }

  // TODO: Test cast
  // Loop over all cast operations
  for (std::set<
           std::pair<CastInst::CastOps, std::pair<Type *, Type *>>>::iterator
           it = CastOpDeclTypes.begin(),
           end = CastOpDeclTypes.end();
       it != end; ++it) {
    // static <u32 x 4> llvm_ZExt_u8x4_u32x4(<u8 x 4> in) { //
    // Src->isVector == Dst->isVector
    //   return convert_<u32 x 4>(in);
    // }
    // static u32 llvm_BitCast_u8x4_u32(<u8 x 4> in) { //
    // Src->bitsSize == Dst->bitsSize
    //   return as_typen(in);
    // }
    CastInst::CastOps opcode = (*it).first;
    Type *SrcTy = (*it).second.first;
    Type *DstTy = (*it).second.second;
    bool SrcSigned, DstSigned;
    switch (opcode) {
    default:
      SrcSigned = false;
      DstSigned = false;
      break;
    case Instruction::SIToFP:
      SrcSigned = true;
      DstSigned = false;
      break;
    case Instruction::FPToSI:
      SrcSigned = false;
      DstSigned = true;
      break;
    case Instruction::SExt:
      SrcSigned = true;
      DstSigned = true;
      break;
    }

    Out << "static ";
    printTypeName(Out, DstTy, false);
    Out << " llvm_" << Instruction::getOpcodeName(opcode) << "_";
    printTypeString(Out, SrcTy);
    Out << "_";
    printTypeString(Out, DstTy);
    Out << "(";
    printTypeName(Out, SrcTy, false);
    Out << " in) {\n";
    if (opcode == Instruction::BitCast) {
      // Reinterpret cast
      cwriter_assert(SrcTy->getPrimitiveSizeInBits() == 
                     DstTy->getPrimitiveSizeInBits());
      if (DstTy->isPointerTy() && SrcTy->isPointerTy()) {
        cwriter_assert(DstTy->getPointerAddressSpace() ==
                       SrcTy->getPointerAddressSpace());
        Out << "  return (";
        printTypeName(Out, DstTy);
        Out << ")in;\n";
      } else if (DstTy->isIntOrIntVectorTy() || DstTy->isFPOrFPVectorTy()) {
        Out << "  return as_";
        printTypeName(Out, DstTy);
        Out << "(in);";
      } else {
        Out << "  union {\n    ";
        printTypeName(Out, SrcTy, false);
        Out << " in;\n    ";
        printTypeName(Out, DstTy, false);
        Out << " out;\n  } cast;\n";
        Out << "  cast.in = in;\n"
            << "  return cast.out;\n";
      }
    } else {
      // Static cast
      if (isa<VectorType>(DstTy)) {
        cwriter_assert(SrcTy->getVectorNumElements() == 
                       DstTy->getVectorNumElements());
      }
      Out << "  return ";
      printPadded(Out, DstTy, [&]() {
        printWithCast(Out, DstTy, false, [&]() {
          printWithCast(Out, DstTy, DstSigned, [&]() {
            printWithCast(Out, SrcTy, SrcSigned, [&]() {
              if (SrcSigned) {
                Out << "in";
              } else {
                printUnpadded(Out, SrcTy, "in");
              }
            }, SrcSigned);
          }, DstSigned);
        });
      });
      Out << ";\n";
    }
    Out << "}\n";
  }

  // Loop over all simple operations
  for (std::set<std::pair<unsigned, Type *>>::iterator
           it = InlineOpDeclTypes.begin(),
           end = InlineOpDeclTypes.end();
       it != end; ++it) {
    // static <u32 x 4> llvm_BinOp_u32x4(<u32 x 4> a, <u32 x 4> b) {
    //   return a OP b;
    // }
    unsigned opcode = (*it).first;
    Type *OpTy = (*it).second;
    bool shouldCast;
    bool isSigned;
    opcodeNeedsCast(opcode, shouldCast, isSigned);

    Out << "static ";
    printTypeName(Out, OpTy);
    Out << " ";
    if (opcode == BinaryNeg || opcode == Instruction::FNeg) {
      Out << "llvm_neg_";
      printTypeString(Out, OpTy);
      Out << "(";
      printTypeName(Out, OpTy);
      Out << " a)";
    } else if (opcode == BinaryNot) {
      Out << "llvm_not_";
      printTypeString(Out, OpTy);
      Out << "(";
      printTypeName(Out, OpTy);
      Out << " a)";
    } else {
      Out << "llvm_" << Instruction::getOpcodeName(opcode) << "_";
      printTypeString(Out, OpTy);
      Out << "(";
      printTypeName(Out, OpTy);
      Out << " a, ";
      printTypeName(Out, OpTy);
      Out << " b)";
    }

    Out << " {\n  return ";


    printPadded(Out, OpTy, [&]() {
      printWithCast(Out, OpTy, false, [&]() {
        if (opcode == BinaryNeg || opcode == BinaryNot || opcode == Instruction::FNeg) {
          switch(opcode) {
          case BinaryNeg:
          case Instruction::FNeg:
            Out << "-";
            break;
          case BinaryNot:
            Out << "~";
            break;
          }
          printWithCast(Out, OpTy, isSigned, "a", isSigned);
        } else if (opcode == Instruction::FRem) {
          // Output a call to fmod instead of emitting a%b
          Out << "fmod(a, b)";
        } else {
          Out << "(";
          if (isSigned) {
            printWithCast(Out, OpTy, isSigned, "a");
          } else if (shouldCast) {
            printUnpadded(Out, OpTy, "a");
          } else {
            Out << "a";
          }
          Out << " ";
          switch (opcode) {
          case Instruction::Add:
          case Instruction::FAdd:
            Out << "+";
            break;
          case Instruction::Sub:
          case Instruction::FSub:
            Out << "-";
            break;
          case Instruction::Mul:
          case Instruction::FMul:
            Out << "*";
            break;
          case Instruction::URem:
          case Instruction::SRem:
          case Instruction::FRem:
            Out << "%";
            break;
          case Instruction::UDiv:
          case Instruction::SDiv:
          case Instruction::FDiv:
            Out << "/";
            break;
          case Instruction::And:
            Out << "&";
            break;
          case Instruction::Or:
            Out << "|";
            break;
          case Instruction::Xor:
            Out << "^";
            break;
          case Instruction::Shl:
            Out << "<<";
            break;
          case Instruction::LShr:
          case Instruction::AShr:
            Out << ">>";
            break;
          default:
            errs() << "Invalid operator type!" << opcode << "\n";
            errorWithMessage("invalid operator type");
          }
          Out << " ";
          if (isSigned) {
            printWithCast(Out, OpTy, isSigned, "b");
          } else if (shouldCast) {
            printUnpadded(Out, OpTy, "b");
          } else {
            Out << "b";
          }
          Out << ")";
        }
      }, isSigned);
    });
    Out << ";\n}\n";
  }
  
  // Loop over all inline constructors
  for (std::set<Type *>::iterator it = CtorDeclTypes.begin(),
                                  end = CtorDeclTypes.end();
       it != end; ++it) {
    // static <u32 x 4> llvm_ctor_u32x4(u32 x1, u32 x2, u32 x3, u32 x4) {
    //   ...
    // }
    Out << "static ";
    printTypeName(Out, *it);
    Out << " llvm_ctor_";
    printTypeString(Out, *it);
    Out << "(";
    StructType *STy = dyn_cast<StructType>(*it);
    ArrayType *ATy = dyn_cast<ArrayType>(*it);
    VectorType *VTy = dyn_cast<VectorType>(*it);
    unsigned e = (STy ? STy->getNumElements()
                      : (ATy ? ATy->getNumElements() : VTy->getNumElements()));
    bool printed = false;
    for (unsigned i = 0; i != e; ++i) {
      Type *ElTy =
          STy ? STy->getElementType(i) : (*it)->getSequentialElementType();
      if (isEmptyType(ElTy))
        Out << " /* ";
      else if (printed)
        Out << ", ";
      printTypeName(Out, ElTy);
      Out << " x" << i;
      if (isEmptyType(ElTy))
        Out << " */";
      else
        printed = true;
    }
    Out << ") {\n";
    if (VTy) {
      // return (<u32 x 4>)(x1, x2, x3, x4);
      unsigned e = VTy->getNumElements();
      Out << "  return (";
      printTypeName(Out, *it);
      Out << ")(";
      for (unsigned i = 0; i != e; ++i) {
        Out << "x" << i;
        if (i < e - 1) {
          Out << ", ";
        }
      }
      Out << ");";
    } else {
      // Rty r = {
      //   x1, x2, x3, x4
      // };
      // return r;
      Out << "  ";
      printTypeName(Out, *it);
      Out << " r;";
      for (unsigned i = 0; i != e; ++i) {
        Type *ElTy =
            STy ? STy->getElementType(i) : (*it)->getSequentialElementType();
        if (isEmptyType(ElTy))
          continue;
        if (STy)
          Out << "\n  r.f" << i << " = x" << i << ";";
        else if (ATy)
          Out << "\n  r.a[" << i << "] = x" << i << ";";
        else
          cwriter_assert(0);
      }
      Out << "\n  return r;";
    }
    Out << "\n}\n";
  }

  // Emit definitions of the intrinsics.
  for (SmallVector<Function *, 16>::iterator I = intrinsicsToDefine.begin(),
                                             E = intrinsicsToDefine.end();
       I != E; ++I) {
    printIntrinsicDefinition(**I, Out);
  }

  if (!M.empty())
    Out << "\n\n/* Function Bodies */\n";
}

void CWriter::declareOneGlobalVariable(GlobalVariable *I) {
  if (I->isDeclaration() || isEmptyType(I->getType()->getPointerElementType()))
    return;

  // Ignore special globals, such as debug info.
  if (getGlobalVariableClass(&*I))
    return;

  if (I->hasLocalLinkage())
    Out << "static ";
  Out << "__constant ";

  Type *ElTy = I->getType()->getElementType();
  unsigned Alignment = I->getAlignment();
  bool IsOveraligned = Alignment && Alignment > TD->getABITypeAlignment(ElTy);

  printTypeName(Out, ElTy, false) << ' ' << GetValueName(I);
  if (IsOveraligned)
    Out << " __attribute__((aligned(" << Alignment << ")))";

  // If the initializer is not null, emit the initializer.
  if (!I->getInitializer()->isNullValue()) {
    Out << " = ";
    writeOperand(I->getInitializer(), ContextStatic);
  }
  Out << ";\n";
}

void CWriter::printFPConstantValue(raw_ostream &Out, const ConstantFP *FPC,
                                   enum OperandContext Context) {
  if (Context == ContextStatic) {
    // Print in decimal form.
    // It can be used in constexpr but may result in precision loss.
    double V;
    const char *postfix = "";
    switch(FPC->getType()->getTypeID()) {
      case Type::FloatTyID:
        V = FPC->getValueAPF().convertToFloat();
        postfix = "f";
        break;
      case Type::DoubleTyID:
        V = FPC->getValueAPF().convertToDouble();
        break;
      default:
        errorWithMessage("Unsupported FP constant type");
    }
    if (std::isnan(V)) {
      errorWithMessage("The value is NaN");
    } else if (std::isinf(V)) {
      errorWithMessage("The value is Inf");
    } else {
      Out << ftostr(FPC->getValueAPF()) << postfix;
    }
  } else {
    // Print in hexadecimal form (as represented in LLVM).
    // To cast it back to float we need to use OpenCL `as_type()` function.
    Out << "as_";
    printTypeName(Out, FPC->getType());
    Out << "(";
    Out << "0x" << utohexstr(FPC->getValueAPF().bitcastToAPInt().getZExtValue());
    switch (FPC->getType()->getTypeID()) {
      case Type::FloatTyID:
        Out << "u";
        break;
      case Type::DoubleTyID:
        Out << "ul";
        break;
      default:
        errorWithMessage("Unsupported FP constant type");
    }
    Out << ")";
  }
}

/// printSymbolTable - Run through symbol table looking for type names.  If a
/// type name is found, emit its declaration...
void CWriter::printModuleTypes(raw_ostream &Out) {
  // Keep track of which types have been printed so far.
  std::set<Type *> TypesPrinted;

  // Loop over all structures then push them into the stack so they are
  // printed in the correct order.
  Out << "\n/* Types Declarations */\n";

  // forward-declare all structs here first

  {
    std::set<Type *> TypesPrinted;
    for (auto it = TypedefDeclTypes.begin(), end = TypedefDeclTypes.end();
         it != end; ++it) {
      forwardDeclareStructs(Out, *it, TypesPrinted);
    }
  }

  Out << "\n/* Function definitions */\n";

  struct FunctionDefinition {
    FunctionType *FT;
    std::vector<FunctionType *> Dependencies;
    std::string NameToPrint;
  };

  std::vector<FunctionDefinition> FunctionTypeDefinitions;
  // Copy Function Types into indexable container
  for (auto &I : UnnamedFunctionIDs) {
    const auto &F = I.first;
    FunctionType *FT = F.first;
    std::vector<FunctionType *> FDeps;
    for (const auto P : F.first->params()) {
      if (P->isPointerTy()) {
        PointerType *PP = dyn_cast<PointerType>(P);
        if (PP->getElementType()->isFunctionTy()) {
          FDeps.push_back(dyn_cast<FunctionType>(PP->getElementType()));
        }
      }
    }
    std::string DeclString;
    raw_string_ostream TmpOut(DeclString);
    printFunctionDeclaration(TmpOut, F.first, F.second);
    TmpOut.flush();
    FunctionTypeDefinitions.emplace_back(
        FunctionDefinition{FT, FDeps, DeclString});
  }

  // Sort function types
  TopologicalSorter Sorter(FunctionTypeDefinitions.size());
  DenseMap<FunctionType *, int> TopologicalSortMap;
  // Add Vertices
  for (unsigned I = 0; I < FunctionTypeDefinitions.size(); I++) {
    TopologicalSortMap[FunctionTypeDefinitions[I].FT] = I;
  }
  // Add Edges
  for (unsigned I = 0; I < FunctionTypeDefinitions.size(); I++) {
    const auto &Dependencies = FunctionTypeDefinitions[I].Dependencies;
    for (unsigned J = 0; J < Dependencies.size(); J++) {
      Sorter.addEdge(I, TopologicalSortMap[Dependencies[J]]);
    }
  }
  Optional<std::vector<int>> TopologicalSortResult = Sorter.sort();
  if (!TopologicalSortResult.hasValue()) {
    errorWithMessage("Cyclic dependencies in function definitions");
  }
  for (const auto I : TopologicalSortResult.getValue()) {
    Out << FunctionTypeDefinitions[I].NameToPrint << "\n";
  }

  // We may have collected some intrinsic prototypes to emit.
  // Emit them now, before the function that uses them is emitted
  for (auto &F : prototypesToGen) {
    Out << '\n';
    printFunctionProto(Out, F);
    Out << ";\n";
  }

  Out << "\n/* Types Definitions */\n";

  for (auto it = TypedefDeclTypes.begin(), end = TypedefDeclTypes.end();
       it != end; ++it) {
    printContainedTypes(Out, *it, TypesPrinted);
  }
}

void CWriter::forwardDeclareStructs(raw_ostream &Out, Type *Ty,
                                    std::set<Type *> &TypesPrinted) {
  if (!TypesPrinted.insert(Ty).second)
    return;
  if (isEmptyType(Ty))
    return;

  for (auto I = Ty->subtype_begin(); I != Ty->subtype_end(); ++I) {
    forwardDeclareStructs(Out, *I, TypesPrinted);
  }

  if (StructType *ST = dyn_cast<StructType>(Ty)) {
    Out << getStructName(ST) << ";\n";
  }
}

// Push the struct onto the stack and recursively push all structs
// this one depends on.
void CWriter::printContainedTypes(raw_ostream &Out, Type *Ty,
                                  std::set<Type *> &TypesPrinted) {
  // Check to see if we have already printed this struct.
  if (!TypesPrinted.insert(Ty).second)
    return;
  // Skip empty structs
  if (isEmptyType(Ty))
    return;

  // Print all contained types first.
  for (Type::subtype_iterator I = Ty->subtype_begin(), E = Ty->subtype_end();
       I != E; ++I)
    printContainedTypes(Out, *I, TypesPrinted);

  if (StructType *ST = dyn_cast<StructType>(Ty)) {
    // Print structure type out.
    printStructDeclaration(Out, ST);
  } else if (ArrayType *AT = dyn_cast<ArrayType>(Ty)) {
    // Print array type out.
    printArrayDeclaration(Out, AT);
  } else if (VectorType *VT = dyn_cast<VectorType>(Ty)) {
    // Print vector type out.
    printVectorDeclaration(Out, VT);
  }
}

static inline bool isFPIntBitCast(Instruction &I) {
  if (!isa<BitCastInst>(I))
    return false;
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DstTy = I.getType();
  return (SrcTy->isFloatingPointTy() && DstTy->isIntegerTy()) ||
         (DstTy->isFloatingPointTy() && SrcTy->isIntegerTy());
}

void CWriter::printFunction(Function &F) {
  cwriter_assert(!F.isDeclaration());
  if (F.hasLocalLinkage())
    Out << "static ";

  iterator_range<Function::arg_iterator> args = F.args();
  printFunctionProto(Out, F.getFunctionType(),
                     std::make_pair(F.getAttributes(), F.getCallingConv()),
                     GetValueName(&F), &args);

  Out << " {\n";

  Function::arg_iterator A = F.arg_begin(), E = F.arg_end();
  if (F.hasStructRetAttr()) {
    Type *StructTy =
        cast<PointerType>(A->getType())->getElementType();
    std::string sret_name = GetValueName(A);
    Out << "  ";
    printTypeName(Out, StructTy)
        << " " << sret_name << "_sret;  /* Struct return temporary */\n";

    Out << "  ";
    printTypeName(Out, A->getType());
    Out << " " << sret_name << " = &" << sret_name << "_sret;\n";

    ++A;
  }
  for (;A != E; ++A) {
    if (A->hasByValAttr()) {
      std::string val_name = GetValueName(A);
      Out << "  ";
      printTypeName(Out, A->getType());
      Out << " " << val_name << " = &" << val_name << "_val;\n";
    }
  }

  bool PrintedVar = false;

  // print local variable information for the function
  for (inst_iterator I = inst_begin(&F), E = inst_end(&F); I != E; ++I) {
    if (AllocaInst *AI = isDirectAlloca(&*I)) {
      unsigned Alignment = AI->getAlignment();
      bool IsOveraligned = Alignment && Alignment > TD->getABITypeAlignment(
                                                        AI->getAllocatedType());
      Out << "  ";

      printTypeName(Out, AI->getAllocatedType()) << ' ';
      Out << GetValueName(AI);
      if (IsOveraligned)
        Out << " __attribute__((aligned(" << Alignment << ")))";
      Out << ";    /* Address-exposed local */\n";
      PrintedVar = true;
    } else if (!isEmptyType(I->getType()) && !isInlinableInst(*I)) {
      Out << "  ";
      printTypeName(Out, I->getType(), false) << ' ' << GetValueName(&*I);
      Out << ";\n";

      if (isa<PHINode>(*I)) { // Print out PHI node temporaries as well...
        Out << "  ";
        printTypeName(Out, I->getType(), false)
            << ' ' << (GetValueName(&*I) + "_phi");
        Out << ";\n";
      }
      PrintedVar = true;
    }
    // We need a temporary for the BitCast to use so it can pluck a value out
    // of a union to do the BitCast. This is separate from the need for a
    // variable to hold the result of the BitCast.
    if (isFPIntBitCast(*I)) {
      errorWithMessage("Bit cast is not implemented");
    }
  }

  if (PrintedVar)
    Out << '\n';

  // print the basic blocks
  for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
    if (Loop *L = LI->getLoopFor(&*BB)) {
      if (L->getHeader() == &*BB && L->getParentLoop() == nullptr)
        printLoop(L);
    } else {
      printBasicBlock(&*BB);
    }
  }

  Out << "}\n\n";
}

void CWriter::printLoop(Loop *L) {
  for (unsigned i = 0, e = L->getBlocks().size(); i != e; ++i) {
    BasicBlock *BB = L->getBlocks()[i];
    Loop *BBLoop = LI->getLoopFor(BB);
    if (BBLoop == L)
      printBasicBlock(BB);
    else if (BB == BBLoop->getHeader() && BBLoop->getParentLoop() == L)
      printLoop(BBLoop);
  }
}

void CWriter::printBasicBlock(BasicBlock *BB) {

  // Don't print the label for the basic block if there are no uses, or if
  // the only terminator use is the predecessor basic block's terminator.
  // We have to scan the use list because PHI nodes use basic blocks too but
  // do not require a label to be generated.
  bool NeedsLabel = false;
  for (pred_iterator PI = pred_begin(BB), E = pred_end(BB); PI != E; ++PI)
    if (isGotoCodeNecessary(*PI, BB)) {
      NeedsLabel = true;
      break;
    }

  if (NeedsLabel)
    Out << GetValueName(BB) << ":\n";

  // Output all of the instructions in the basic block...
  for (BasicBlock::iterator II = BB->begin(), E = --BB->end(); II != E; ++II) {
    DILocation *Loc = (*II).getDebugLoc();
    if (Loc != nullptr && LastAnnotatedSourceLine != Loc->getLine()) {
      Out << "#line " << Loc->getLine() << " \"" << Loc->getDirectory() << "/" << Loc->getFilename() << "\"" << "\n";
      LastAnnotatedSourceLine = Loc->getLine();
    }
    if (!isInstIgnored(*II) && !isInlinableInst(*II) && !isDirectAlloca(&*II)) {
      if (!isEmptyType(II->getType()))
        outputLValue(&*II);
      else
        Out << "  ";
      writeInstComputationInline(*II);
      Out << ";\n";
    }
  }

  // Don't emit prefix or suffix for the terminator.
  visit(*BB->getTerminator());
}

// Specific Instruction type classes... note that all of the casts are
// necessary because we use the instruction classes as opaque types...
void CWriter::visitReturnInst(ReturnInst &I) {
  CurInstr = &I;

  // If this is a struct return function, return the temporary struct.
  Function *F = I.getParent()->getParent();
  if (F->hasStructRetAttr()) {
    Out << "  return " << GetValueName(F->arg_begin()) << "_sret;\n";
    return;
  }

  // Don't output a void return if this is the last basic block in the function
  // unless that would make the basic block empty
  if (I.getNumOperands() == 0 &&
      &*--I.getParent()->getParent()->end() == I.getParent() &&
      &*I.getParent()->begin() != &I) {
    return;
  }

  Out << "  return";
  if (I.getNumOperands()) {
    Out << ' ';
    writeOperand(I.getOperand(0));
  }
  Out << ";\n";
}

void CWriter::visitSwitchInst(SwitchInst &SI) {
  CurInstr = &SI;

  Value *Cond = SI.getCondition();
  unsigned NumBits = cast<IntegerType>(Cond->getType())->getBitWidth();

  if (SI.getNumCases() == 0) { // unconditional branch
    printPHICopiesForSuccessor(SI.getParent(), SI.getDefaultDest(), 2);
    printBranchToBlock(SI.getParent(), SI.getDefaultDest(), 2);
    Out << "\n";

  } else if (NumBits <= 64) { // model as a switch statement
    Out << "  switch (";
    writeOperand(Cond);
    Out << ") {\n  default:\n";
    printPHICopiesForSuccessor(SI.getParent(), SI.getDefaultDest(), 2);
    printBranchToBlock(SI.getParent(), SI.getDefaultDest(), 2);

    // Skip the first item since that's the default case.
    for (SwitchInst::CaseIt i = SI.case_begin(), e = SI.case_end(); i != e;
         ++i) {
      ConstantInt *CaseVal = i->getCaseValue();
      BasicBlock *Succ = i->getCaseSuccessor();
      Out << "  case ";
      writeOperand(CaseVal);
      Out << ":\n";
      printPHICopiesForSuccessor(SI.getParent(), Succ, 2);
      if (isGotoCodeNecessary(SI.getParent(), Succ))
        printBranchToBlock(SI.getParent(), Succ, 2);
      else
        Out << "    break;\n";
    }
    Out << "  }\n";

  } else { // model as a series of if statements
    Out << "  ";
    for (SwitchInst::CaseIt i = SI.case_begin(), e = SI.case_end(); i != e;
         ++i) {
      Out << "if (";
      ConstantInt *CaseVal = i->getCaseValue();
      BasicBlock *Succ = i->getCaseSuccessor();
      ICmpInst *icmp = new ICmpInst(CmpInst::ICMP_EQ, Cond, CaseVal);
      visitICmpInst(*icmp);
      delete icmp;
      Out << ") {\n";
      printPHICopiesForSuccessor(SI.getParent(), Succ, 2);
      printBranchToBlock(SI.getParent(), Succ, 2);
      Out << "  } else ";
    }
    Out << "{\n";
    printPHICopiesForSuccessor(SI.getParent(), SI.getDefaultDest(), 2);
    printBranchToBlock(SI.getParent(), SI.getDefaultDest(), 2);
    Out << "  }\n";
  }
  Out << "\n";
}

bool CWriter::isGotoCodeNecessary(BasicBlock *From, BasicBlock *To) {
  /// FIXME: This should be reenabled, but loop reordering safe!!
  return true;

  if (std::next(Function::iterator(From)) != Function::iterator(To))
    return true; // Not the direct successor, we need a goto.

  // isa<SwitchInst>(From->getTerminator())

  if (LI->getLoopFor(From) != LI->getLoopFor(To))
    return true;
  return false;
}

void CWriter::printPHICopiesForSuccessor(BasicBlock *CurBlock,
                                         BasicBlock *Successor,
                                         unsigned Indent) {
  for (BasicBlock::iterator I = Successor->begin(); isa<PHINode>(I); ++I) {
    PHINode *PN = cast<PHINode>(I);
    // Now we have to do the printing.
    Value *IV = PN->getIncomingValueForBlock(CurBlock);
    if (!isa<UndefValue>(IV) && !isEmptyType(IV->getType())) {
      Out << std::string(Indent, ' ');
      Out << "  " << GetValueName(&*I) << "_phi = ";
      writeOperand(IV);
      Out << ";   /* for PHI node */\n";
    }
  }
}

void CWriter::printBranchToBlock(BasicBlock *CurBB, BasicBlock *Succ,
                                 unsigned Indent) {
  if (isGotoCodeNecessary(CurBB, Succ)) {
    Out << std::string(Indent, ' ') << "  goto ";
    writeOperand(Succ);
    Out << ";\n";
  }
}

// Branch instruction printing - Avoid printing out a branch to a basic block
// that immediately succeeds the current one.
void CWriter::visitBranchInst(BranchInst &I) {
  CurInstr = &I;

  if (I.isConditional()) {
    if (isGotoCodeNecessary(I.getParent(), I.getSuccessor(0))) {
      Out << "  if (";
      writeOperand(I.getCondition());
      Out << ") {\n";

      printPHICopiesForSuccessor(I.getParent(), I.getSuccessor(0), 2);
      printBranchToBlock(I.getParent(), I.getSuccessor(0), 2);

      if (isGotoCodeNecessary(I.getParent(), I.getSuccessor(1))) {
        Out << "  } else {\n";
        printPHICopiesForSuccessor(I.getParent(), I.getSuccessor(1), 2);
        printBranchToBlock(I.getParent(), I.getSuccessor(1), 2);
      }
    } else {
      // First goto not necessary, assume second one is...
      Out << "  if (!";
      writeOperand(I.getCondition());
      Out << ") {\n";

      printPHICopiesForSuccessor(I.getParent(), I.getSuccessor(1), 2);
      printBranchToBlock(I.getParent(), I.getSuccessor(1), 2);
    }

    Out << "  }\n";
  } else {
    printPHICopiesForSuccessor(I.getParent(), I.getSuccessor(0), 0);
    printBranchToBlock(I.getParent(), I.getSuccessor(0), 0);
  }
  Out << "\n";
}

// PHI nodes get copied into temporary values at the end of predecessor basic
// blocks.  We now need to copy these temporary values into the REAL value for
// the PHI.
void CWriter::visitPHINode(PHINode &I) {
  CurInstr = &I;

  writeOperand(&I);
  Out << "_phi";
}

void CWriter::visitUnaryOperator(UnaryOperator &I) {
  CurInstr = &I;
  Type *Ty = I.getOperand(0)->getType();
  unsigned opcode = I.getOpcode();
  switch (opcode) {
  case Instruction::FNeg:
    Out << "llvm_neg_";
    printTypeString(Out, Ty);
    Out << "(";
    writeOperand(I.getOperand(0));
    Out << ")";
    InlineOpDeclTypes.insert(std::pair<unsigned, Type *>(opcode, Ty));
    break;
  default:
    errs() << "Unknown unary operator: " << I << "\n";
    errorWithMessage("Unknown unary operator");
  }
}

void CWriter::visitBinaryOperator(BinaryOperator &I) {
  using namespace PatternMatch;

  CurInstr = &I;

  // binary instructions, shift instructions, setCond instructions.
  cwriter_assert(!I.getType()->isPointerTy());

  Type *Ty = I.getOperand(0)->getType();
  unsigned opcode;
  Value *X;
  if (match(&I, m_Neg(m_Value(X))) || match(&I, m_FNeg(m_Value(X)))) {
    opcode = BinaryNeg;
    Out << "llvm_neg_";
    printTypeString(Out, Ty);
    Out << "(";
    writeOperand(X);
  } else if (match(&I, m_Not(m_Value(X)))) {
    opcode = BinaryNot;
    Out << "llvm_not_";
    printTypeString(Out, Ty);
    Out << "(";
    writeOperand(X);
  } else {
    opcode = I.getOpcode();
    Out << "llvm_" << Instruction::getOpcodeName(opcode) << "_";
    printTypeString(Out, Ty);
    Out << "(";
    writeOperand(I.getOperand(0));
    Out << ", ";
    writeOperand(I.getOperand(1));
  }
  Out << ")";
  InlineOpDeclTypes.insert(std::pair<unsigned, Type *>(opcode, Ty));
}

void CWriter::visitICmpInst(ICmpInst &I) {
  CurInstr = &I;

  Out << "llvm_icmp_" << getCmpPredicateName(I.getPredicate()) << "_";
  printTypeString(Out, I.getOperand(0)->getType());
  Out << "(";
  writeOperand(I.getOperand(0));
  Out << ", ";
  writeOperand(I.getOperand(1));
  Out << ")";

  CmpDeclTypes.insert(
      std::pair<CmpInst::Predicate, Type *>(I.getPredicate(), I.getOperand(0)->getType()));

  if (VectorType *VTy = dyn_cast<VectorType>(I.getOperand(0)->getType())) {
    TypedefDeclTypes.insert(
        I.getType()); // insert type not necessarily visible above
  }
}

void CWriter::visitFCmpInst(FCmpInst &I) {
  CurInstr = &I;

  Out << "llvm_fcmp_" << getCmpPredicateName(I.getPredicate()) << "_";
  printTypeString(Out, I.getOperand(0)->getType());
  Out << "(";
  writeOperand(I.getOperand(0));
  Out << ", ";
  writeOperand(I.getOperand(1));
  Out << ")";

  CmpDeclTypes.insert(
      std::pair<CmpInst::Predicate, Type *>(I.getPredicate(), I.getOperand(0)->getType()));

  if (VectorType *VTy = dyn_cast<VectorType>(I.getOperand(0)->getType())) {
    TypedefDeclTypes.insert(
        I.getType()); // insert type not necessarily visible above
  }
}

void CWriter::visitCastInst(CastInst &I) {
  CurInstr = &I;

  Type *DstTy = I.getType();
  Type *SrcTy = I.getOperand(0)->getType();

  Out << "llvm_" << I.getOpcodeName() << "_";
  printTypeString(Out, SrcTy);
  Out << "_";
  printTypeString(Out, DstTy);
  Out << "(";
  writeOperand(I.getOperand(0));
  Out << ")";
  CastOpDeclTypes.insert(
      std::pair<Instruction::CastOps, std::pair<Type *, Type *>>(
          I.getOpcode(), std::pair<Type *, Type *>(SrcTy, DstTy)));
}

void CWriter::visitSelectInst(SelectInst &I) {
  CurInstr = &I;

  cwriter_assert(
      !I.getCondition()->getType()->isVectorTy() ||
      I.getType()->isVectorTy());

  Out << "llvm_select_";
  printTypeString(Out, I.getCondition()->getType());
  Out << "_";
  printTypeString(Out, I.getType());
  Out << "(";
  writeOperand(I.getCondition());
  Out << ", ";
  writeOperand(I.getTrueValue());
  Out << ", ";
  writeOperand(I.getFalseValue());
  Out << ")";
  SelectDeclTypes.insert(std::make_pair(
    I.getCondition()->getType(),
    I.getType()
  ));
}

bool CWriter::isInstIgnored(Instruction &I) const {
  if (CallInst *CI = dyn_cast<CallInst>(&I)) {
    if (Function *F = CI->getCalledFunction()) {
      if (F->isIntrinsic() && isIntrinsicIgnored(F->getIntrinsicID())) {
        return true;
      }
    }
  }
  return false;
}

bool CWriter::isIntrinsicIgnored(unsigned ID) const {
  switch (ID) {
  case Intrinsic::not_intrinsic:
  case Intrinsic::dbg_value:
  case Intrinsic::dbg_declare:
    return true;
  default:
    return false;
  }
}

void CWriter::printIntrinsicDefinition(FunctionType *funT, unsigned Opcode,
                                       std::string OpName, raw_ostream &Out) {
  const CLIntrinsic *intr = intrinsics.get(Opcode);
  if (intr) {
    intr->printDefinition(
      Out, funT, OpName,
      [this](raw_ostream &Out, Type *Ty, bool isSigned) {
        printTypeName(Out, Ty, isSigned);
      }
    );
  } else {
    errs() << "Unsupported Intrinsic: " << Opcode << "\n";
    errorWithMessage("unsupported instrinsic");
  }
}

void CWriter::printIntrinsicDefinition(Function &F, raw_ostream &Out) {
  FunctionType *funT = F.getFunctionType();
  unsigned Opcode = F.getIntrinsicID();
  std::string OpName = GetValueName(&F);
  printIntrinsicDefinition(funT, Opcode, OpName, Out);
}

bool CWriter::lowerIntrinsics(Function &F) {
  bool LoweredAny = false;

  // Examine all the instructions in this function to find the intrinsics that
  // need to be lowered.
  for (auto &BB : F) {
    for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E;) {
      if (CallInst *CI = dyn_cast<CallInst>(I++)) {
        if (Function *F = CI->getCalledFunction()) {
          if (
            F->isIntrinsic() &&
            !isIntrinsicIgnored(F->getIntrinsicID()) &&
            !intrinsics.hasImpl(F->getIntrinsicID())
          ) {
            // All other intrinsic calls we must lower.
            LoweredAny = true;

            Instruction *Before = (CI == &BB.front())
                                      ? nullptr
                                      : &*std::prev(BasicBlock::iterator(CI));

            IL->LowerIntrinsicCall(CI);
            if (Before) { // Move iterator to instruction after call
              I = BasicBlock::iterator(Before);
              ++I;
            } else {
              I = BB.begin();
            }

            // If the intrinsic got lowered to another call, and that call has
            // a definition, then we need to make sure its prototype is emitted
            // before any calls to it.
            if (CallInst *Call = dyn_cast<CallInst>(I))
              if (Function *NewF = Call->getCalledFunction())
                if (!NewF->isDeclaration())
                  prototypesToGen.push_back(NewF);
          }
        }
      }
    }
  }

  return LoweredAny;
}

void CWriter::visitCallInst(CallInst &I) {
  CurInstr = &I;

  // Handle intrinsic function calls first...
  if (Function *F = I.getCalledFunction()) {
    auto ID = F->getIntrinsicID();
    if (ID != Intrinsic::not_intrinsic && visitBuiltinCall(I, ID))
      return;
  }

  Value *Callee = I.getCalledValue();

  PointerType *PTy = cast<PointerType>(Callee->getType());
  FunctionType *FTy = cast<FunctionType>(PTy->getElementType());

  // If this is a call to a struct-return function, assign to the first
  // parameter instead of passing it to the call.
  const AttributeList &PAL = I.getAttributes();
  bool isStructRet = I.hasStructRetAttr();
  if (isStructRet) {
    writeOperandDeref(I.getArgOperand(0));
    Out << " = ";
  }

  if (I.isTailCall())
    Out << " /*tail*/ ";

  writeOperand(Callee);

  Out << '(';

  bool PrintedArg = false;
  if (FTy->isVarArg() && !FTy->getNumParams()) {
    Out << "0 /*dummy arg*/";
    PrintedArg = true;
  }

  unsigned NumDeclaredParams = FTy->getNumParams();
  CallSite CS(&I);
  CallSite::arg_iterator AI = CS.arg_begin(), AE = CS.arg_end();
  unsigned ArgNo = 0;
  if (isStructRet) { // Skip struct return argument.
    ++AI;
    ++ArgNo;
  }

  for (; AI != AE; ++AI, ++ArgNo) {
    if (PrintedArg)
      Out << ", ";
    if (ArgNo < NumDeclaredParams &&
        (*AI)->getType() != FTy->getParamType(ArgNo)) {
      Out << '(';
      printTypeName(
          Out, FTy->getParamType(ArgNo),
          /*isSigned=*/PAL.hasAttribute(ArgNo + 1, Attribute::SExt));
      Out << ')';
    }
    // Check if the argument is expected to be passed by value.
    if (I.getAttributes().hasAttribute(ArgNo + 1, Attribute::ByVal))
      writeOperandDeref(*AI);
    else
      writeOperand(*AI);
    PrintedArg = true;
  }
  Out << ')';
}

/// visitBuiltinCall - Handle the call to the specified builtin.  Returns true
/// if the entire call is handled, return false if it wasn't handled
bool CWriter::visitBuiltinCall(CallInst &I, Intrinsic::ID ID) {
  CurInstr = &I;

  if (isIntrinsicIgnored(ID)) {
    return true;
  } else if (intrinsics.hasImpl(ID)) {
    return false;
  } else {
    errs() << "Unsupported LLVM intrinsic: " << I << "\n";
    errorWithMessage("Unsupported llvm intrinsic");
  }
}

// TODO_: Simplify expressions in cases of zero indices
void CWriter::printGEPExpression(Value *Ptr, gep_type_iterator I,
                                 gep_type_iterator E) {
  Out.flush();
  OutModifier mod(_Out); // writeOperand always print to Out
  // We use this workaround to extract written data from Out

  Type *IntoT = I.getIndexedType();
  Out << "(";
  writeOperand(Ptr);
  if (I != E) {
    Out << " + ";
    writeOperandWithCast(I.getOperand(), Instruction::GetElementPtr);
    ++I;
  }
  Out << ")";

  for (; I != E; ++I) {
    cwriter_assert(I.getOperand()->getType()->isIntegerTy());
    // TODO: indexing a Vector with a Vector is valid,
    // but we don't support it here

    Out.flush();
    std::string prev = mod.cut_tail(); // Extract Out changes

    if (IntoT->isStructTy()) {
      Out << "(&" << prev << "->f"
          << cast<ConstantInt>(I.getOperand())->getZExtValue()
          << ")";
    } else if (IntoT->isArrayTy()) {
      Out << "(&" << prev << "->a[";
      writeOperandWithCast(I.getOperand(), Instruction::GetElementPtr);
      Out << "])";
    } else if (IntoT->isVectorTy()) {
      Out << "(&((";
      printTypeName(Out, 
        IntoT->getVectorElementType()->getPointerTo(
          Ptr->getType()->getPointerAddressSpace()));
      Out << ")" << prev << ")[";
      writeOperandWithCast(I.getOperand(), Instruction::GetElementPtr);
      Out << "])";
    } else {
      Out << "(&" << prev << "[";
      writeOperandWithCast(I.getOperand(), Instruction::GetElementPtr);
      Out << "])";
    }

    IntoT = I.getIndexedType();
  }
}

void CWriter::writeMemoryAccess(Value *Operand, Type *OperandType,
                                bool IsVolatile, unsigned Alignment /*bytes*/) {
  if (isAddressExposed(Operand) && !IsVolatile) {
    writeOperandInternal(Operand);
    return;
  }

  if (Alignment && Alignment < TD->getABITypeAlignment(OperandType)) {
    outs() << "Warning: unaligned memory access: " << *CurInstr;
    outs() << " ; ";
    CurInstr->getDebugLoc().print(outs());
    outs() << "\n";
    //errorWithMessage("Unaligned memory access is restricted");
  }

  Out << '*';
  if (IsVolatile) {
    Out << "(volatile ";
    printTypeName(Out, OperandType, false);
    Out << "*)";
  }

  writeOperand(Operand);
}

void CWriter::visitLoadInst(LoadInst &I) {
  CurInstr = &I;

  printPadded(Out, I.getType(), [&]() {
    writeMemoryAccess(I.getOperand(0), I.getType(), I.isVolatile(),
                      I.getAlignment());
  });
}

void CWriter::visitStoreInst(StoreInst &I) {
  CurInstr = &I;

  writeMemoryAccess(I.getPointerOperand(), I.getOperand(0)->getType(),
                    I.isVolatile(), I.getAlignment());
  Out << " = ";
  Value *Operand = I.getOperand(0);
  printUnpadded(Out, Operand->getType(), [&]() {
    writeOperand(Operand);
  });
}

void CWriter::visitGetElementPtrInst(GetElementPtrInst &I) {
  CurInstr = &I;

  printGEPExpression(I.getPointerOperand(), gep_type_begin(I), gep_type_end(I));
}

void CWriter::visitInsertElementInst(InsertElementInst &I) {
  CurInstr = &I;

  // Start by copying the entire aggregate value into the result variable.
  writeOperand(I.getOperand(0));
  Type *EltTy = I.getType()->getElementType();
  cwriter_assert(I.getOperand(1)->getType() == EltTy);
  if (isEmptyType(EltTy))
    return;

  // Then do the insert to update the field.
  Out << ";\n  ";
  Out << GetValueName(&I) << ".";
  int index;
  OutModifier mod(Out.str());
  writeOperand(I.getOperand(2));
  Out.str();
  std::string index_str = mod.cut_tail();
  std::stringstream ss(index_str);
  ss >> index;
  if (!ss.good()) {
    errs() << "Cannot parse '" << index_str << "' as integer\n";
    errorWithMessage("Cannot access vector element by dynamic index");
  }
  printVectorComponent(Out, index);
  Out << " = ";
  writeOperand(I.getOperand(1));
}

void CWriter::visitExtractElementInst(ExtractElementInst &I) {
  CurInstr = &I;

  cwriter_assert(!isEmptyType(I.getType()));
  if (isa<UndefValue>(I.getOperand(0))) {
    Out << "(";
    printTypeName(Out, I.getType());
    Out << ") 0/*UNDEF*/";
  } else {
    Out << "(";
    writeOperand(I.getOperand(0));
    Out << ").";
    int index;
    OutModifier mod(Out.str());
    writeOperand(I.getOperand(1));
    Out.str();
    std::string index_str = mod.cut_tail();
    std::stringstream ss(index_str);
    ss >> index;
    if (!ss.good()) {
      errs() << "Cannot parse '" << index_str << "' as integer\n";
      errorWithMessage("Cannot access vector element by dynamic index");
    }
    printVectorComponent(Out, index);
  }
}

// TODO_: Shuffle vector using something like `.xyxw`
// <result> = shufflevector <n x <ty>> <v1>, <n x <ty>> <v2>, <m x i32> <mask>
// ; yields <m x <ty>>
void CWriter::visitShuffleVectorInst(ShuffleVectorInst &SVI) {
  CurInstr = &SVI;

  VectorType *VT = SVI.getType();
  Type *EltTy = VT->getElementType();
  VectorType *InputVT = cast<VectorType>(SVI.getOperand(0)->getType());
  cwriter_assert(!isEmptyType(VT));
  cwriter_assert(InputVT->getElementType() == VT->getElementType());

  CtorDeclTypes.insert(VT);
  Out << "llvm_ctor_";
  printTypeString(Out, VT);
  Out << "(";

  Constant *Zero = Constant::getNullValue(EltTy);
  unsigned NumElts = VT->getNumElements();
  unsigned NumInputElts = InputVT->getNumElements(); // n
  for (unsigned i = 0; i != NumElts; ++i) {
    if (i)
      Out << ", ";
    int SrcVal = SVI.getMaskValue(i);
    if ((unsigned)SrcVal >= NumInputElts * 2) {
      Out << "/*undef*/";
      printConstant(Zero);
    } else {
      // If SrcVal belongs [0, n - 1], it extracts value from <v1>
      // If SrcVal belongs [n, 2 * n - 1], it extracts value from <v2>
      // In C++, the value false is converted to zero and the value true is
      // converted to one
      Value *Op = SVI.getOperand((unsigned)SrcVal >= NumInputElts);
      if (isa<Constant>(Op)) {
        if (isa<ConstantAggregateZero>(Op) || isa<UndefValue>(Op)) {
          printConstant(Zero);
        } else if (isa<ConstantVector>(Op)) {
          printConstant(
              cast<ConstantVector>(Op)->getOperand(SrcVal & (NumElts - 1)),
              ContextNormal);
        } else {
          errorWithMessage("Unknown constant value");
        }
      } else if (isa<Argument>(Op) || isa<Instruction>(Op)) {
        // Do an extractelement of this value from the appropriate input.
        Out << "(";
        writeOperand(Op);
        Out << ").";
        printVectorComponent(Out,
          ((unsigned)SrcVal >= NumInputElts ? SrcVal - NumInputElts : SrcVal)
        );
      } else {
        errorWithMessage("Unknown value");
      }
    }
  }
  Out << ")";
}

void CWriter::visitInsertValueInst(InsertValueInst &IVI) {
  CurInstr = &IVI;

  // Start by copying the entire aggregate value into the result variable.
  writeOperand(IVI.getOperand(0));
  Type *EltTy = IVI.getOperand(1)->getType();
  if (isEmptyType(EltTy))
    return;

  // Then do the insert to update the field.
  Out << ";\n  ";
  Out << GetValueName(&IVI);
  for (const unsigned *b = IVI.idx_begin(), *i = b, *e = IVI.idx_end(); i != e;
       ++i) {
    Type *IndexedTy = ExtractValueInst::getIndexedType(
        IVI.getOperand(0)->getType(), makeArrayRef(b, i));
    cwriter_assert(IndexedTy);
    if (IndexedTy->isArrayTy())
      Out << ".a[" << *i << "]";
    else
      Out << ".f" << *i;
  }
  Out << " = ";
  writeOperand(IVI.getOperand(1));
}

void CWriter::visitExtractValueInst(ExtractValueInst &EVI) {
  CurInstr = &EVI;

  Out << "(";
  if (isa<UndefValue>(EVI.getOperand(0))) {
    Out << "(";
    printTypeName(Out, EVI.getType());
    Out << ") 0/*UNDEF*/";
  } else {
    writeOperand(EVI.getOperand(0));
    for (const unsigned *b = EVI.idx_begin(), *i = b, *e = EVI.idx_end();
         i != e; ++i) {
      Type *IndexedTy = ExtractValueInst::getIndexedType(
          EVI.getOperand(0)->getType(), makeArrayRef(b, i));
      if (IndexedTy->isArrayTy())
        Out << ".a[" << *i << "]";
      else
        Out << ".f" << *i;
    }
  }
  Out << ")";
}

LLVM_ATTRIBUTE_NORETURN void CWriter::errorWithMessage(const char *message) const {
  errs() << message;
  errs() << " in:\n";
  if (CurInstr != nullptr) {
    errs() << *CurInstr << "\nat ";
    CurInstr->getDebugLoc().print(errs());
  } else {
    errs() << "<unknown instruction>";
  }
  errs() << "\n\n";

  sys::PrintStackTrace(errs());
  
  exit(1);
  llvm_unreachable(message);
}

} // namespace llvm_opencl
