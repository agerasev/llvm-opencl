//===-- CBackend.cpp - Library for converting LLVM code to C --------------===//
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


#include "CBackend.h"
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
#include "llvm/Demangle/Demangle.h"

#include "TopologicalSorter.h"

#include <algorithm>
#include <cstdio>

#include <iostream>
#include <sstream>

#undef NDEBUG

// Jackson Korba 9/29/14
#ifndef DEBUG_TYPE
#define DEBUG_TYPE ""
#endif
// End Modification

// Some ms header decided to define setjmp as _setjmp, undo this for this file
// since we don't need it
#ifdef setjmp
#undef setjmp
#endif

namespace llvm_cbe {

using namespace llvm;

extern "C" void LLVMInitializeCBackendTarget() {
  // Register the target.
  RegisterTargetMachine<CTargetMachine> X(TheCBackendTarget);
}

char CWriter::ID = 0;

// extra (invalid) Ops tags for tracking unary ops as a special case of the
// available binary ops
enum UnaryOps {
  BinaryNeg = Instruction::OtherOpsEnd + 1,
  BinaryNot,
};

#ifdef NDEBUG
#define cwriter_assert(expr)                                                   \
  do {                                                                         \
  } while (0)
#else
#define cwriter_assert(expr)                                                   \
  if (!(expr)) {                                                               \
    this->errorWithMessage(#expr);                                             \
  }
#endif

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

bool CWriter::isEmptyType(Type *Ty) const { return llvm_cbe::isEmptyType(Ty); }

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

  // Output all floating point constants that cannot be printed accurately.
  printFloatingPointConstants(F);

  printFunction(F);

  LI = nullptr;

  return Modified;
}

static std::string CBEMangle(const std::string &S) {
  std::string Result;

  for (auto c : S) {
    if (isalnum(c) || c == '_') {
      Result += c;
    } else {
      Result += '_';
      Result += 'A' + (c & 15);
      Result += 'A' + ((c >> 4) & 15);
      Result += '_';
    }
  }

  return Result;
}

raw_ostream &CWriter::printTypeString(raw_ostream &Out, Type *Ty,
                                      bool isSigned) {
  if (StructType *ST = dyn_cast<StructType>(Ty)) {
    cwriter_assert(!isEmptyType(ST));
    TypedefDeclTypes.insert(Ty);

    if (!ST->isLiteral() && !ST->getName().empty())
      return Out << "struct_" << CBEMangle(ST->getName());

    unsigned id = UnnamedStructIDs.getOrInsert(ST);
    return Out << "unnamed_" + utostr(id);
  }

  if (Ty->isPointerTy()) {
    Out << "p";
    return printTypeString(Out, Ty->getPointerElementType(), isSigned);
  }

  switch (Ty->getTypeID()) {
  case Type::VoidTyID:
    return Out << "void";
  case Type::IntegerTyID: {
    unsigned NumBits = cast<IntegerType>(Ty)->getBitWidth();
    if (NumBits == 1)
      return Out << "bool";
    else {
      cwriter_assert(NumBits <= 128 && "Bit widths > 128 not implemented yet");
      return Out << (isSigned ? "i" : "u") << NumBits;
    }
  }
  case Type::FloatTyID:
    return Out << "f32";
  case Type::DoubleTyID:
    return Out << "f64";
  case Type::X86_FP80TyID:
    return Out << "f80";
  case Type::PPC_FP128TyID:
  case Type::FP128TyID:
    return Out << "f128";

  case Type::X86_MMXTyID:
    return Out << (isSigned ? "i32y2" : "u32y2");

  case Type::VectorTyID: {
    TypedefDeclTypes.insert(Ty);
    VectorType *VTy = cast<VectorType>(Ty);
    cwriter_assert(VTy->getNumElements() != 0);
    printTypeString(Out, VTy->getElementType(), isSigned);
    return Out << "x" << VTy->getNumElements();
  }

  case Type::ArrayTyID: {
    TypedefDeclTypes.insert(Ty);
    ArrayType *ATy = cast<ArrayType>(Ty);
    cwriter_assert(ATy->getNumElements() != 0);
    printTypeString(Out, ATy->getElementType(), isSigned);
    return Out << "a" << ATy->getNumElements();
  }

  default:
#ifndef NDEBUG
    errs() << "Unknown primitive type: " << *Ty << "\n";
#endif
    errorWithMessage("unknown primitive type");
  }
}

std::string CWriter::getStructName(StructType *ST) {
  cwriter_assert(ST->getNumElements() != 0);
  if (!ST->isLiteral() && !ST->getName().empty())
    return "struct l_struct_" + CBEMangle(ST->getName().str());

  unsigned id = UnnamedStructIDs.getOrInsert(ST);
  return "struct l_unnamed_" + utostr(id);
}

std::string
CWriter::getFunctionName(FunctionType *FT,
                         std::pair<AttributeList, CallingConv::ID> PAL) {
  unsigned id = UnnamedFunctionIDs.getOrInsert(std::make_pair(FT, PAL));
  return "l_fptr_" + utostr(id);
}

std::string CWriter::getArrayName(ArrayType *AT) {
  std::string astr;
  raw_string_ostream ArrayInnards(astr);
  // Arrays are wrapped in structs to allow them to have normal
  // value semantics (avoiding the array "decay").
  cwriter_assert(!isEmptyType(AT));
  printTypeName(ArrayInnards, AT->getElementType(), false);
  return "struct l_array_" + utostr(AT->getNumElements()) + '_' +
         CBEMangle(ArrayInnards.str());
}

std::string CWriter::getVectorName(VectorType *VT, bool Aligned) {
  std::string astr;
  raw_string_ostream VectorInnards(astr);
  // Vectors are handled like arrays
  cwriter_assert(!isEmptyType(VT));

  uint64_t n = VT->getNumElements();
  if (n != 2 && n != 3 && n != 4 && n != 8 && n != 16) {
#ifndef NDEBUG
    errs() << "Vector of length " << n << " not supported\n";
#endif
    errorWithMessage("Unsupported vector length");
  }

  printTypeName(VectorInnards, VT->getElementType(), false);
  std::string t = CBEMangle(VectorInnards.str());
  if (t != "char" && t != "uchar" && t != "short" && t != "ushort" &&
      t != "int" && t != "uint" && t != "long" && t != "ulong" &&
      t != "float" && t != "double") {
#ifndef NDEBUG
    errs() << "Vector of type " << t << " not supported\n";
#endif
    errorWithMessage("Unsupported vector type");
  }
  
  return t + utostr(n);
}

static const std::string getCmpPredicateName(CmpInst::Predicate P) {
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
#ifndef NDEBUG
    errs() << "Invalid icmp predicate!" << P << "\n";
#endif
    // TODO: cwriter_assert
    llvm_unreachable(0);
  }
}

static const char *getFCmpImplem(CmpInst::Predicate P) {
  switch (P) {
  case FCmpInst::FCMP_FALSE:
    return "0";
  case FCmpInst::FCMP_OEQ:
    return "X == Y";
  case FCmpInst::FCMP_OGT:
    return "X >  Y";
  case FCmpInst::FCMP_OGE:
    return "X >= Y";
  case FCmpInst::FCMP_OLT:
    return "X <  Y";
  case FCmpInst::FCMP_OLE:
    return "X <= Y";
  case FCmpInst::FCMP_ONE:
    return "X != Y && llvm_fcmp_ord(X, Y);";
  case FCmpInst::FCMP_ORD:
    return "X == X && Y == Y";
  case FCmpInst::FCMP_UNO:
    return "X != X || Y != Y";
  case FCmpInst::FCMP_UEQ:
    return "X == Y || llvm_fcmp_uno(X, Y)";
  case FCmpInst::FCMP_UGT:
    return "X >  Y || llvm_fcmp_uno(X, Y)";
    return "ugt";
  case FCmpInst::FCMP_UGE:
    return "X >= Y || llvm_fcmp_uno(X, Y)";
  case FCmpInst::FCMP_ULT:
    return "X <  Y || llvm_fcmp_uno(X, Y)";
  case FCmpInst::FCMP_ULE:
    return "X <= Y || llvm_fcmp_uno(X, Y)";
  case FCmpInst::FCMP_UNE:
    return "X != Y";
  case FCmpInst::FCMP_TRUE:
    return "1";
  default:
#ifndef NDEBUG
    errs() << "Invalid fcmp predicate!" << P << "\n";
#endif
    // TODO: cwriter_assert
    llvm_unreachable(0);
  }
}

static void defineFCmpOp(raw_ostream &Out, CmpInst::Predicate const P) {
  Out << "static int llvm_fcmp_" << getCmpPredicateName(P)
      << "(double X, double Y) { ";
  Out << "return " << getFCmpImplem(P) << "; }\n";
}

void CWriter::headerUseFCmpOp(CmpInst::Predicate P) {
  switch (P) {
  case FCmpInst::FCMP_ONE:
    FCmpOps.insert(CmpInst::FCMP_ORD);
    break;
  case FCmpInst::FCMP_UEQ:
  case FCmpInst::FCMP_UGT:
  case FCmpInst::FCMP_UGE:
  case FCmpInst::FCMP_ULT:
  case FCmpInst::FCMP_ULE:
    FCmpOps.insert(CmpInst::FCMP_UNO);
    break;
  default:
    break;
  }
  FCmpOps.insert(P);
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
      return Out << "bool";
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
#ifndef NDEBUG
    errs() << "Unknown primitive type: " << *Ty;
#endif
    errorWithMessage("unknown primitive type");
  }
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
    switch (Ty->getPointerAddressSpace()) {
      case 0:
        Out << "__private ";
        break;
      case 1:
        Out << "__global ";
        break;
      case 2:
        Out << "__constant ";
        break;
      case 3:
        Out << "__local ";
        break;
      default:
#ifndef NDEBUG
      errs() << "Invalid address space " << Ty->getPointerAddressSpace() << "\n";
#endif
      errorWithMessage("Encountered Invalid Address Space");
      break;
    }
    return printTypeName(Out, ElTy, false) << '*';
  }

  case Type::ArrayTyID: {
    TypedefDeclTypes.insert(Ty);
    return Out << getArrayName(cast<ArrayType>(Ty));
  }

  case Type::VectorTyID: {
    TypedefDeclTypes.insert(Ty);
    return Out << getVectorName(cast<VectorType>(Ty), true);
  }

  default:
#ifndef NDEBUG
    errs() << "Unexpected type: " << *Ty << "\n";
#endif
    errorWithMessage("unexpected type");
  }
}

raw_ostream &CWriter::printTypeNameUnaligned(raw_ostream &Out, Type *Ty,
                                             bool isSigned) {
  if (VectorType *VTy = dyn_cast<VectorType>(Ty)) {
    // MSVC doesn't handle __declspec(align) on parameters,
    // but we specify it for Vector (hoping the compiler will vectorize it)
    // so we need to avoid it sometimes
    TypedefDeclTypes.insert(VTy);
    return Out << getVectorName(VTy, false);
  }
  return printTypeName(Out, Ty, isSigned);
}

raw_ostream &CWriter::printStructDeclaration(raw_ostream &Out,
                                             StructType *STy) {
  if (STy->isPacked())
    Out << "#ifdef _MSC_VER\n#pragma pack(push, 1)\n#endif\n";
  Out << getStructName(STy) << " {\n";
  unsigned Idx = 0;
  for (StructType::element_iterator I = STy->element_begin(),
                                    E = STy->element_end();
       I != E; ++I, Idx++) {
    Out << "  ";
    bool empty = isEmptyType(*I);
    if (empty)
      Out << "/* "; // skip zero-sized types
    printTypeName(Out, *I, false) << " field" << utostr(Idx);
    if (empty)
      Out << " */"; // skip zero-sized types
    else
      Out << ";\n";
  }
  Out << '}';
  if (STy->isPacked())
    Out << " __attribute__ ((packed))";
  Out << ";\n";
  if (STy->isPacked())
    Out << "#ifdef _MSC_VER\n#pragma pack(pop)\n#endif\n";
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
CWriter::printFunctionProto(raw_ostream &Out, FunctionType *FTy,
                            std::pair<AttributeList, CallingConv::ID> Attrs,
                            const std::string &Name,
                            iterator_range<Function::arg_iterator> *ArgList) {
  AttributeList &PAL = Attrs.first;

  // Should this function actually return a struct by-value?
  bool isStructReturn = PAL.hasAttribute(1, Attribute::StructRet) ||
                        PAL.hasAttribute(2, Attribute::StructRet);
  // Get the return type for the function.
  Type *RetTy;
  if (!isStructReturn)
    RetTy = FTy->getReturnType();
  else {
    // If this is a struct-return function, print the struct-return type.
    RetTy = cast<PointerType>(FTy->getParamType(0))->getElementType();
  }
  printTypeName(Out, RetTy,
                /*isSigned=*/
                PAL.hasAttribute(AttributeList::ReturnIndex, Attribute::SExt));

  switch (Attrs.second) {
  case CallingConv::C:
    break;
  case CallingConv::SPIR_FUNC:
    break;
  case CallingConv::SPIR_KERNEL:
    Out << " __kernel";
    break;
  case CallingConv::X86_StdCall:
    Out << " __stdcall";
    break;
  case CallingConv::X86_FastCall:
    Out << " __fastcall";
    break;
  case CallingConv::X86_ThisCall:
    Out << " __thiscall";
    break;
  default:
#ifndef NDEBUG
    errs() << "Unhandled calling convention " << Attrs.second << "\n";
#endif
    errorWithMessage("Encountered Unhandled Calling Convention");
    break;
  }
  Out << ' ' << Name << '(';

  unsigned Idx = 1;
  bool PrintedArg = false;
  FunctionType::param_iterator I = FTy->param_begin(), E = FTy->param_end();
  Function::arg_iterator ArgName =
      ArgList ? ArgList->begin() : Function::arg_iterator();

  // If this is a struct-return function, don't print the hidden
  // struct-return argument.
  if (isStructReturn) {
    cwriter_assert(I != E && "Invalid struct return function!");
    ++I;
    ++Idx;
    if (ArgList)
      ++ArgName;
  }

  for (; I != E; ++I) {
    Type *ArgTy = *I;
    if (PAL.hasAttribute(Idx, Attribute::ByVal)) {
      cwriter_assert(ArgTy->isPointerTy());
      ArgTy = cast<PointerType>(ArgTy)->getElementType();
    }
    if (PrintedArg)
      Out << ", ";
    printTypeNameUnaligned(Out, ArgTy,
                           /*isSigned=*/PAL.hasAttribute(Idx, Attribute::SExt));
    PrintedArg = true;
    ++Idx;
    if (ArgList) {
      Out << ' ' << GetValueName(ArgName);
      ++ArgName;
    }
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
  Out << " array[" << utostr(ATy->getNumElements()) << "];\n};\n";
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
#ifndef NDEBUG
    errs() << "Vector component index is "  << i << " but it cannot be greater than 15\n";
#endif
    errorWithMessage("Vector component error");
  }
  return Out;
}

raw_ostream &CWriter::printVectorShuffled(raw_ostream &Out, const std::vector<uint64_t> &mask) {
  static const char comps[17] = "0123456789ABCDEF";
  size_t s = mask.size();
  if (s != 2 && s != 3 && s != 4 && s != 8 && s != 16) {
#ifndef NDEBUG
    errs() << "Shuffled vector size is "  << s << " but it can only be 2, 3, 4, 8 or 16\n";
#endif
    errorWithMessage("Shuffled vector size error");
  }
  Out << "s";
  for (uint64_t i : mask) {
    if (i < 16) {
      Out << comps[i];
    } else {
  #ifndef NDEBUG
      errs() << "Vector component index is "  << i << " but it cannot be greater than 15\n";
  #endif
      errorWithMessage("Vector component error");
    }
  }
  return Out;
}

// isFPCSafeToPrint - Returns true if we may assume that CFP may be written out
// textually as a double (rather than as a reference to a stack-allocated
// variable). We decide this by converting CFP to a string and back into a
// double, and then checking whether the conversion results in a bit-equal
// double to the original value of CFP. This depends on us and the target C
// compiler agreeing on the conversion process (which is pretty likely since we
// only deal in IEEE FP).

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

static bool isFPCSafeToPrint(const ConstantFP *CFP) {
  bool ignored;
  // Do long doubles in hex for now.
  if (CFP->getType() != Type::getFloatTy(CFP->getContext()) &&
      CFP->getType() != Type::getDoubleTy(CFP->getContext()))
    return false;
  APFloat APF = APFloat(CFP->getValueAPF()); // copy
  if (CFP->getType() == Type::getFloatTy(CFP->getContext()))
    APF.convert(APFloat::IEEEdouble(), APFloat::rmNearestTiesToEven, &ignored);
#if HAVE_PRINTF_A && ENABLE_CBE_PRINTF_A
  char Buffer[100];
  sprintf(Buffer, "%a", APF.convertToDouble());
  if (!strncmp(Buffer, "0x", 2) || !strncmp(Buffer, "-0x", 3) ||
      !strncmp(Buffer, "+0x", 3))
    return APF.bitwiseIsEqual(APFloat(atof(Buffer)));
  return false;
#else
  std::string StrVal = ftostr(APF);

  while (StrVal[0] == ' ')
    StrVal.erase(StrVal.begin());

  // Check to make sure that the stringized number is not some string like "Inf"
  // or NaN.  Check that the string matches the "[-+]?[0-9]" regex.
  if ((StrVal[0] >= '0' && StrVal[0] <= '9') ||
      ((StrVal[0] == '-' || StrVal[0] == '+') &&
       (StrVal[1] >= '0' && StrVal[1] <= '9')))
    // Reparse stringized version!
    return APF.bitwiseIsEqual(APFloat(atof(StrVal.c_str())));
  return false;
#endif
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
    // TODO: VectorType are valid here, but not supported
    if (!CE->getType()->isIntegerTy() && !CE->getType()->isFloatingPointTy() &&
        !CE->getType()->isPointerTy()) {
#ifndef NDEBUG
      errs() << "Unsupported constant type " << *CE->getType() << " in: " << *CE
             << "\n";
#endif
      errorWithMessage("Unsupported constant type");
    }
    switch (CE->getOpcode()) {
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::FPTrunc:
    case Instruction::FPExt:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::PtrToInt:
    case Instruction::IntToPtr:
    case Instruction::BitCast:
      Out << "(";
      printCast(CE->getOpcode(), CE->getOperand(0)->getType(), CE->getType());
      if (CE->getOpcode() == Instruction::SExt &&
          CE->getOperand(0)->getType() == Type::getInt1Ty(CPV->getContext())) {
        // Make sure we really sext from bool here by subtracting from 0
        Out << "0-";
      }
      printConstant(CE->getOperand(0), ContextCasted);
      if (CE->getType() == Type::getInt1Ty(CPV->getContext()) &&
          (CE->getOpcode() == Instruction::Trunc ||
           CE->getOpcode() == Instruction::FPToUI ||
           CE->getOpcode() == Instruction::FPToSI ||
           CE->getOpcode() == Instruction::PtrToInt)) {
        // Make sure we really truncate to bool here by anding with 1
        Out << "&1u";
      }
      Out << ')';
      return;

    case Instruction::GetElementPtr:
      Out << "(";
      printGEPExpression(CE->getOperand(0), gep_type_begin(CPV),
                         gep_type_end(CPV));
      Out << ")";
      return;
    case Instruction::Select:
      Out << '(';
      printConstant(CE->getOperand(0), ContextCasted);
      Out << '?';
      printConstant(CE->getOperand(1), ContextNormal);
      Out << ':';
      printConstant(CE->getOperand(2), ContextNormal);
      Out << ')';
      return;
    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::SDiv:
    case Instruction::UDiv:
    case Instruction::FDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::ICmp:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr: {
      Out << '(';
      bool NeedsClosingParens = printConstExprCast(CE);
      printConstantWithCast(CE->getOperand(0), CE->getOpcode());
      switch (CE->getOpcode()) {
      case Instruction::Add:
      case Instruction::FAdd:
        Out << " + ";
        break;
      case Instruction::Sub:
      case Instruction::FSub:
        Out << " - ";
        break;
      case Instruction::Mul:
      case Instruction::FMul:
        Out << " * ";
        break;
      case Instruction::URem:
      case Instruction::SRem:
      case Instruction::FRem:
        Out << " % ";
        break;
      case Instruction::UDiv:
      case Instruction::SDiv:
      case Instruction::FDiv:
        Out << " / ";
        break;
      case Instruction::And:
        Out << " & ";
        break;
      case Instruction::Or:
        Out << " | ";
        break;
      case Instruction::Xor:
        Out << " ^ ";
        break;
      case Instruction::Shl:
        Out << " << ";
        break;
      case Instruction::LShr:
      case Instruction::AShr:
        Out << " >> ";
        break;
      case Instruction::ICmp:
        switch (CE->getPredicate()) {
        case ICmpInst::ICMP_EQ:
          Out << " == ";
          break;
        case ICmpInst::ICMP_NE:
          Out << " != ";
          break;
        case ICmpInst::ICMP_SLT:
        case ICmpInst::ICMP_ULT:
          Out << " < ";
          break;
        case ICmpInst::ICMP_SLE:
        case ICmpInst::ICMP_ULE:
          Out << " <= ";
          break;
        case ICmpInst::ICMP_SGT:
        case ICmpInst::ICMP_UGT:
          Out << " > ";
          break;
        case ICmpInst::ICMP_SGE:
        case ICmpInst::ICMP_UGE:
          Out << " >= ";
          break;
        default:
          errorWithMessage("Illegal ICmp predicate");
        }
        break;
      default:
        errorWithMessage("Illegal opcode here!");
      }
      printConstantWithCast(CE->getOperand(1), CE->getOpcode());
      if (NeedsClosingParens)
        Out << "))";
      Out << ')';
      return;
    }
    case Instruction::FCmp: {
      Out << '(';
      bool NeedsClosingParens = printConstExprCast(CE);
      if (CE->getPredicate() == FCmpInst::FCMP_FALSE)
        Out << "0";
      else if (CE->getPredicate() == FCmpInst::FCMP_TRUE)
        Out << "1";
      else {
        const auto Pred = (CmpInst::Predicate)CE->getPredicate();
        headerUseFCmpOp(Pred);
        Out << "llvm_fcmp_" << getCmpPredicateName(Pred) << "(";
        printConstant(CE->getOperand(0), ContextCasted);
        Out << ", ";
        printConstant(CE->getOperand(1), ContextCasted);
        Out << ")";
      }
      if (NeedsClosingParens)
        Out << "))";
      Out << ')';
      return;
    }
    default:
#ifndef NDEBUG
      errs() << "CWriter Error: Unhandled constant expression: " << *CE << "\n";
#endif
      errorWithMessage("unhandled constant expression");
    }
  } else if (isa<UndefValue>(CPV) && CPV->getType()->isSingleValueType()) {
    if (CPV->getType()->isVectorTy()) {
      if (Context == ContextStatic) {
        Out << "{}";
        return;
      }
      VectorType *VT = cast<VectorType>(CPV->getType());
      cwriter_assert(!isEmptyType(VT));
      CtorDeclTypes.insert(VT);
      Out << "/*undef*/llvm_ctor_";
      printTypeString(Out, VT, false);
      Out << "(";
      Constant *Zero = Constant::getNullValue(VT->getElementType());
      unsigned NumElts = VT->getNumElements();
      for (unsigned i = 0; i != NumElts; ++i) {
        if (i)
          Out << ", ";
        printConstant(Zero, ContextCasted);
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
      Out << (CI->getZExtValue() ? '1' : '0');
    } else if (Context != ContextNormal && ActiveBits < 64 &&
               Ty->getPrimitiveSizeInBits() < 64 &&
               ActiveBits < Ty->getPrimitiveSizeInBits()) {
      if (ActiveBits >= 32)
        Out << "INT64_C(";
      Out << CI->getSExtValue(); // most likely a shorter representation
      if (ActiveBits >= 32)
        Out << ")";
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
      Out << "UINT64_C(" << CI->getZExtValue() << ")";
    } else {
      errorWithMessage("Integers larger than 64 bits are not supported");
    }
    return;
  }

  switch (CPV->getType()->getTypeID()) {
  case Type::FloatTyID:
  case Type::DoubleTyID:
  case Type::X86_FP80TyID:
  case Type::PPC_FP128TyID:
  case Type::FP128TyID: {
    ConstantFP *FPC = cast<ConstantFP>(CPV);
    auto I = FPConstantMap.find(FPC);
    if (I != FPConstantMap.end()) {
      // Because of FP precision problems we must load from a stack allocated
      // value that holds the value in hex.
      Out << "(*("
          << (FPC->getType() == Type::getFloatTy(CPV->getContext())
                  ? "float"
                  : FPC->getType() == Type::getDoubleTy(CPV->getContext())
                        ? "double"
                        : "long double")
          << "*)&FPConstant" << I->second << ')';
    } else {
      double V;
      if (FPC->getType() == Type::getFloatTy(CPV->getContext()))
        V = FPC->getValueAPF().convertToFloat();
      else if (FPC->getType() == Type::getDoubleTy(CPV->getContext()))
        V = FPC->getValueAPF().convertToDouble();
      else {
        // Long double.  Convert the number to double, discarding precision.
        // This is not awesome, but it at least makes the CBE output somewhat
        // useful.
        APFloat Tmp = FPC->getValueAPF();
        bool LosesInfo;
        Tmp.convert(APFloat::IEEEdouble(), APFloat::rmTowardZero, &LosesInfo);
        V = Tmp.convertToDouble();
      }

      if (std::isnan(V)) {
        errorWithMessage("The value is NaN");
      } else if (std::isinf(V)) {
        errorWithMessage("The value is Inf");
      } else {
        std::string Num;
#if HAVE_PRINTF_A && ENABLE_CBE_PRINTF_A
        // Print out the constant as a floating point number.
        char Buffer[100];
        sprintf(Buffer, "%a", V);
        Num = Buffer;
#else
        Num = ftostr(FPC->getValueAPF());
#endif
        Out << Num;
      }
    }
    break;
  }

  case Type::ArrayTyID: {
    if (printConstantString(CPV, Context))
      break;
    ArrayType *AT = cast<ArrayType>(CPV->getType());
    cwriter_assert(AT->getNumElements() != 0 && !isEmptyType(AT));
    if (Context != ContextStatic) {
      CtorDeclTypes.insert(AT);
      Out << "llvm_ctor_";
      printTypeString(Out, AT, false);
      Out << "(";
      Context = ContextCasted;
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
      printTypeString(Out, VT, false);
      Out << "(";
      Context = ContextCasted;
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
      printTypeString(Out, ST, false);
      Out << "(";
      Context = ContextCasted;
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

  case Type::PointerTyID:
    if (isa<ConstantPointerNull>(CPV)) {
      Out << "((";
      printTypeName(Out, CPV->getType()); // sign doesn't matter
      Out << ")/*NULL*/0)";
      break;
    } else if (GlobalValue *GV = dyn_cast<GlobalValue>(CPV)) {
      writeOperand(GV);
      break;
    }
    // FALL THROUGH
  default:
#ifndef NDEBUG
    errs() << "Unknown constant type: " << *CPV << "\n";
#endif
    errorWithMessage("unknown constant type");
  }
}

// Some constant expressions need to be casted back to the original types
// because their operands were casted to the expected type. This function takes
// care of detecting that case and printing the cast for the ConstantExpr.
bool CWriter::printConstExprCast(ConstantExpr *CE) {
  bool NeedsExplicitCast = false;
  Type *Ty = CE->getOperand(0)->getType();
  bool TypeIsSigned = false;
  switch (CE->getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
    // We need to cast integer arithmetic so that it is always performed
    // as unsigned, to avoid undefined behavior on overflow.
  case Instruction::LShr:
  case Instruction::URem:
  case Instruction::UDiv:
    NeedsExplicitCast = true;
    break;
  case Instruction::AShr:
  case Instruction::SRem:
  case Instruction::SDiv:
    NeedsExplicitCast = true;
    TypeIsSigned = true;
    break;
  case Instruction::SExt:
    Ty = CE->getType();
    NeedsExplicitCast = true;
    TypeIsSigned = true;
    break;
  case Instruction::ZExt:
  case Instruction::Trunc:
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::UIToFP:
  case Instruction::SIToFP:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::BitCast:
    Ty = CE->getType();
    NeedsExplicitCast = true;
    break;
  default:
    break;
  }
  if (NeedsExplicitCast) {
    Out << "((";
    printTypeName(Out, Ty, TypeIsSigned); // not integer, sign doesn't matter
    Out << ")(";
  }
  return NeedsExplicitCast;
}

//  Print a constant assuming that it is the operand for a given Opcode. The
//  opcodes that care about sign need to cast their operands to the expected
//  type before the operation proceeds. This function does the casting.
void CWriter::printConstantWithCast(Constant *CPV, unsigned Opcode) {

  // Extract the operand's type, we'll need it.
  Type *OpTy = CPV->getType();
  // TODO: VectorType are valid here, but not supported
  if (!OpTy->isIntegerTy() && !OpTy->isFloatingPointTy()) {
#ifndef NDEBUG
    errs() << "Unsupported 'constant with cast' type " << *OpTy
           << " in: " << *CPV << "\n"
           << "\n";
#endif
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
    printConstant(CPV, ContextCasted);
    Out << ")";
  } else
    printConstant(CPV, ContextCasted);
}

std::string CWriter::GetValueName(Value *Operand) {

  // Resolve potential alias.
  if (GlobalAlias *GA = dyn_cast<GlobalAlias>(Operand)) {
    Operand = GA->getAliasee();
  }

  std::string Name = Operand->getName();
  if (Name.empty()) { // Assign unique names to local temporaries.
    unsigned No = AnonValueNumbers.getOrInsert(Operand);
    Name = "tmp__" + utostr(No);
  }

  // Mangle globals with the standard mangler interface for LLC compatibility.
  if (isa<GlobalValue>(Operand)) {
    return CBEMangle(Name);
  }

  std::string VarName;
  VarName.reserve(Name.capacity());

  for (std::string::iterator I = Name.begin(), E = Name.end(); I != E; ++I) {
    unsigned char ch = *I;

    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '_')) {
      char buffer[5];
      sprintf(buffer, "_%x_", ch);
      VarName += buffer;
    } else
      VarName += ch;
  }

  return VarName;
}

/// writeInstComputationInline - Emit the computation for the specified
/// instruction inline, with no destination provided.
void CWriter::writeInstComputationInline(Instruction &I) {
  // C can't handle non-power-of-two integer types
  unsigned mask = 0;
  Type *Ty = I.getType();
  if (Ty->isIntegerTy()) {
    IntegerType *ITy = static_cast<IntegerType *>(Ty);
    if (!ITy->isPowerOf2ByteWidth())
      mask = ITy->getBitMask();
  }

  // If this is a non-trivial bool computation, make sure to truncate down to
  // a 1 bit value.  This is important because we want "add i1 x, y" to return
  // "0" when x and y are true, not "2" for example.
  // Also truncate odd bit sizes
  if (mask)
    Out << "((";

  visit(&I);

  if (mask)
    Out << ")&" << mask << ")";
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
    writeOperand(Operand, ContextCasted);
    Out << ")";
  } else
    writeOperand(Operand, ContextCasted);
}

// Write the operand with a cast to another type based on the icmp predicate
// being used.
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

static void defineConstantDoubleTy(raw_ostream &Out) {
  Out << "typedef ulong ConstantDoubleTy;\n";
}

static void defineConstantFloatTy(raw_ostream &Out) {
  Out << "typedef uint ConstantFloatTy;\n";
}

static void defineUnalignedLoad(raw_ostream &Out) {
  // Define unaligned-load helper macro
  Out << "#define __UNALIGNED_LOAD__(type, align, op) ((struct { type data "
         "__attribute__((packed, aligned(align))); }*)op)->data\n";
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

// PrintEscapedString - Print each character of the specified string, escaping
// it if it is not printable or if it is an escape char.
static void PrintEscapedString(const char *Str, unsigned Length,
                               raw_ostream &Out) {
  for (unsigned i = 0; i != Length; ++i) {
    unsigned char C = Str[i];
    if (isprint(C) && C != '\\' && C != '"')
      Out << C;
    else if (C == '\\')
      Out << "\\\\";
    else if (C == '\"')
      Out << "\\\"";
    else if (C == '\t')
      Out << "\\t";
    else
      Out << "\\x" << hexdigit(C >> 4) << hexdigit(C & 0x0F);
  }
}

// PrintEscapedString - Print each character of the specified string, escaping
// it if it is not printable or if it is an escape char.
static void PrintEscapedString(const std::string &Str, raw_ostream &Out) {
  PrintEscapedString(Str.c_str(), Str.size(), Out);
}

// generateCompilerSpecificCode - This is where we add conditional compilation
// directives to cater to specific compilers as need be.
void CWriter::generateCompilerSpecificCode(raw_ostream &Out,
                                           const DataLayout *) const {
  if (headerIncConstantDoubleTy())
    defineConstantDoubleTy(Out);
  if (headerIncConstantFloatTy())
    defineConstantFloatTy(Out);
  if (headerIncUnalignedLoad())
    defineUnalignedLoad(Out);
}

bool CWriter::doInitialization(Module &M) {
  TheModule = &M;

  TD = new DataLayout(&M);
  IL = new IntrinsicLowering(*TD);
  IL->AddPrototypes(M);

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

  FPConstantMap.clear();
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

  Out << "\n\n/* Global Declarations */\n";

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

  // Global variable declarations...
  if (!M.global_empty()) {
    Out << "\n/* External Global Variable Declarations */\n";
    for (Module::global_iterator I = M.global_begin(), E = M.global_end();
         I != E; ++I) {
      if (!I->isDeclaration() ||
          isEmptyType(I->getType()->getPointerElementType()))
        continue;

      if (I->hasDLLImportStorageClass())
        Out << "__declspec(dllimport) ";
      else if (I->hasDLLExportStorageClass())
        Out << "__declspec(dllexport) ";

      if (I->hasExternalLinkage() || I->hasExternalWeakLinkage() ||
          I->hasCommonLinkage())
        Out << "extern ";
      else
        continue; // Internal Global

      // Thread Local Storage
      if (I->isThreadLocal())
        Out << "__thread ";

      Type *ElTy = I->getType()->getElementType();
      unsigned Alignment = I->getAlignment();
      bool IsOveraligned =
          Alignment && Alignment > TD->getABITypeAlignment(ElTy);

      printTypeName(Out, ElTy, false) << ' ' << GetValueName(&*I);
      if (IsOveraligned)
        Out << " __attribute__((aligned(" << Alignment << ")))";

      Out << ";\n";
    }
  }

  // Function declarations
  Out << "\n/* Function Declarations */\n";

  // Store the intrinsics which will be declared/defined below.
  SmallVector<Function *, 16> intrinsicsToDefine;

  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I) {
    // Don't print declarations for intrinsic functions.
    // Store the used intrinsics, which need to be explicitly defined.
    if (I->isIntrinsic()) {
      switch (I->getIntrinsicID()) {
      default:
        continue;
      case Intrinsic::uadd_with_overflow:
      case Intrinsic::sadd_with_overflow:
      case Intrinsic::usub_with_overflow:
      case Intrinsic::ssub_with_overflow:
      case Intrinsic::umul_with_overflow:
      case Intrinsic::smul_with_overflow:
      case Intrinsic::bswap:
      case Intrinsic::ceil:
      case Intrinsic::ctlz:
      case Intrinsic::ctpop:
      case Intrinsic::cttz:
      case Intrinsic::fabs:
      case Intrinsic::floor:
      case Intrinsic::fma:
      case Intrinsic::fmuladd:
      case Intrinsic::pow:
      case Intrinsic::powi:
      case Intrinsic::rint:
      case Intrinsic::sqrt:
      case Intrinsic::trunc:
        intrinsicsToDefine.push_back(&*I);
        continue;
      }
    }

    // Skip OpenCL built-in functions
    bool builtin = false;
    std::string name;
    ItaniumPartialDemangler dmg;
    if (dmg.partialDemangle(I->getName().data())) {
#ifndef NDEBUG
      // errs() << "Cannot demangle function '" << I->getName() << "'\n";
#endif
    } else {
      size_t size = 0;
      char *buf = dmg.getFunctionName(nullptr, &size);
      if (buf == nullptr) {
        errorWithMessage("Demangle error");
      }
      name = std::string(buf);
      buf = dmg.finishDemangle(buf, &size);
      if (buf == nullptr) {
        errorWithMessage("Demangle error");
      }
      Out << "// " << buf << "\n";
      builtin = builtins.isBuiltIn(buf);
      std::free(buf);
    }

    if (builtin) {
      Out << "#define " << I->getName() << " " << name << "\n";
    } else {
      if (I->hasLocalLinkage())
        Out << "static ";
      if (I->hasExternalWeakLinkage())
        Out << "extern ";
      printFunctionProto(Out, &*I);
      Out << ";\n";
    }

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
  for (std::set<Type *>::iterator it = SelectDeclTypes.begin(),
                                  end = SelectDeclTypes.end();
       it != end; ++it) {
    // #define llvm_select_u8x4(a, b, c) ((a) ? (b) : (c))
    Out << "#define llvm_select_";
    printTypeString(Out, *it, false);
    Out << "(a, b, c) ((a) ? (b) : (c))\n";
  }

  // Loop over all compare operations
  for (std::set<std::pair<CmpInst::Predicate, VectorType *>>::iterator
           it = CmpDeclTypes.begin(),
           end = CmpDeclTypes.end();
       it != end; ++it) {
    // #define llvm_icmp_ge_u8x4(l, r) ((l) >= (r))
    bool isSigned = CmpInst::isSigned((*it).first);
    Out << "#define ";
    const auto Pred = (*it).first;
    if (CmpInst::isFPPredicate((*it).first)) {
      FCmpOps.insert(Pred);
      Out << "llvm_fcmp_";
    } else
      Out << "llvm_icmp_";
    Out << getCmpPredicateName(Pred) << "_";
    printTypeString(Out, (*it).second, isSigned);
    Out << "(l, r) ";
    Out << "((l) ";
    switch ((*it).first) {
    case CmpInst::ICMP_EQ:
      Out << "==";
      break;
    case CmpInst::ICMP_NE:
      Out << "!=";
      break;
    case CmpInst::ICMP_ULE:
    case CmpInst::ICMP_SLE:
      Out << "<=";
      break;
    case CmpInst::ICMP_UGE:
    case CmpInst::ICMP_SGE:
      Out << ">=";
      break;
    case CmpInst::ICMP_ULT:
    case CmpInst::ICMP_SLT:
      Out << "<";
      break;
    case CmpInst::ICMP_UGT:
    case CmpInst::ICMP_SGT:
      Out << ">";
      break;
    default:
#ifndef NDEBUG
      errs() << "Invalid cmp predicate!" << (*it).first << "\n";
#endif
      errorWithMessage("invalid cmp predicate");
    }
    Out << " (r))\n";
  }

  // TODO: Test cast
  // Loop over all (vector) cast operations
  for (std::set<
           std::pair<CastInst::CastOps, std::pair<Type *, Type *>>>::iterator
           it = CastOpDeclTypes.begin(),
           end = CastOpDeclTypes.end();
       it != end; ++it) {
    // static <u32 x 4> llvm_ZExt_u8x4_u32x4(<u8 x 4> in) { //
    // Src->isVector == Dst->isVector
    //   Rty out = {
    //     in[0],
    //     in[1],
    //     in[2],
    //     in[3]
    //   };
    //   return out;
    // }
    // static u32 llvm_BitCast_u8x4_u32(<u8 x 4> in) { //
    // Src->bitsSize == Dst->bitsSize
    //   union {
    //     <u8 x 4> in;
    //     u32 out;
    //   } cast;
    //   cast.in = in;
    //   return cast.out;
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
    printTypeName(Out, DstTy, DstSigned);
    Out << " llvm_" << Instruction::getOpcodeName(opcode) << "_";
    printTypeString(Out, SrcTy, false);
    Out << "_";
    printTypeString(Out, DstTy, false);
    Out << "(";
    printTypeNameUnaligned(Out, SrcTy, SrcSigned);
    Out << " in) {\n";
    if (opcode == Instruction::BitCast) {
      Out << "  union {\n    ";
      printTypeName(Out, SrcTy, SrcSigned);
      Out << " in;\n    ";
      printTypeName(Out, DstTy, DstSigned);
      Out << " out;\n  } cast;\n";
      Out << "  cast.in = in;\n  return cast.out;\n}\n";
    } else if (isa<VectorType>(DstTy)) {
      Out << "  ";
      printTypeName(Out, DstTy, DstSigned);
      Out << " out;\n";
      unsigned n, l = DstTy->getVectorNumElements();
      cwriter_assert(SrcTy->getVectorNumElements() == l);
      for (n = 0; n < l; n++) {
        Out << "  out.";
        printVectorComponent(Out, n);
        Out << " = in.";
        printVectorComponent(Out, n);
        Out << ";\n";
      }
      Out << "  return out;\n}\n";
    } else {
      Out << "#ifndef __emulate_i128\n";
      // easy case first: compiler supports i128 natively
      Out << "  return in;\n";
      Out << "#else\n";
      Out << "  ";
      printTypeName(Out, DstTy, DstSigned);
      Out << " out;\n";
      Out << "  LLVM";
      switch (opcode) {
      case Instruction::UIToFP:
        Out << "UItoFP";
        break;
      case Instruction::SIToFP:
        Out << "SItoFP";
        break;
      case Instruction::Trunc:
        Out << "Trunc";
        break;
      // case Instruction::FPExt:
      // case Instruction::FPTrunc:
      case Instruction::ZExt:
        Out << "ZExt";
        break;
      case Instruction::FPToUI:
        Out << "FPtoUI";
        break;
      case Instruction::SExt:
        Out << "SExt";
        break;
      case Instruction::FPToSI:
        Out << "FPtoSI";
        break;
      default:
        errorWithMessage("Invalid cast opcode for i128");
      }
      Out << "(" << SrcTy->getPrimitiveSizeInBits() << ", &in, "
          << DstTy->getPrimitiveSizeInBits() << ", &out);\n";
      Out << "  return out;\n";
      Out << "#endif\n";
      Out << "}\n";
    }
  }

  // Loop over all simple vector operations
  for (std::set<std::pair<unsigned, Type *>>::iterator
           it = InlineOpDeclTypes.begin(),
           end = InlineOpDeclTypes.end();
       it != end; ++it) {
    // #define llvm_BinOp_u32x4(a, b) ((a) OP (b))
    unsigned opcode = (*it).first;
    Type *OpTy = (*it).second;
    //Type *ElemTy = isa<VectorType>(OpTy) ? OpTy->getVectorElementType() : OpTy;

    Out << "#define ";
    if (opcode == BinaryNeg) {
      Out << "llvm_neg_";
      printTypeString(Out, OpTy, false);
      Out << "(a) ";
    } else if (opcode == BinaryNot) {
      Out << "llvm_not_";
      printTypeString(Out, OpTy, false);
      Out << "(a) ";
    } else {
      Out << "llvm_" << Instruction::getOpcodeName(opcode) << "_";
      printTypeString(Out, OpTy, false);
      Out << "(a, b) ";
    }

    if (opcode == BinaryNeg) {
      Out << "(-(a))";
    } else if (opcode == BinaryNot) {
      Out << "(~(a))";
    } else if (opcode == Instruction::FRem) {
      // Output a call to fmod/fmodf instead of emitting a%b
      Out << "fmod((a), (b))";
    } else {
      Out << "((a) ";
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
#ifndef NDEBUG
        errs() << "Invalid operator type!" << opcode << "\n";
#endif
        errorWithMessage("invalid operator type");
      }
      Out << " (b))";
    }
    Out << "\n";
  }
  
  // Loop over all inline constructors
  for (std::set<Type *>::iterator it = CtorDeclTypes.begin(),
                                  end = CtorDeclTypes.end();
       it != end; ++it) {

    VectorType *VTy = dyn_cast<VectorType>(*it);
    if (VTy) {
      //#define llvm_ctor_u32x4(x1, x2, x3, x4) (Rty)((x1), (x2), (x3), (x4)))
      unsigned e = VTy->getNumElements();
      Out << "#define ";
      Out << "llvm_ctor_";
      printTypeString(Out, *it, false);
      Out << "(";
      for (unsigned i = 0; i != e; ++i) {
        Out << "x" << i;
        if (i < e - 1) {
          Out << ", ";
        }
      }
      Out << ") ((";
      printTypeName(Out, *it);
      Out << ")(";
      for (unsigned i = 0; i != e; ++i) {
        Out << "x" << i;
        if (i < e - 1) {
          Out << ", ";
        }
      }
      Out << "))\n";
    } else {
      // static <u32 x 4> llvm_ctor_u32x4(u32 x1, u32 x2, u32 x3,
      // u32 x4) {
      //   Rty r = {
      //     x1, x2, x3, x4
      //   };
      //   return r;
      // }
      Out << "static ";
      printTypeName(Out, *it);
      Out << " llvm_ctor_";
      printTypeString(Out, *it, false);
      Out << "(";
      StructType *STy = dyn_cast<StructType>(*it);
      ArrayType *ATy = dyn_cast<ArrayType>(*it);
      unsigned e = (STy ? STy->getNumElements() : ATy->getNumElements());
      bool printed = false;
      for (unsigned i = 0; i != e; ++i) {
        Type *ElTy =
            STy ? STy->getElementType(i) : (*it)->getSequentialElementType();
        if (isEmptyType(ElTy))
          Out << " /* ";
        else if (printed)
          Out << ", ";
        printTypeNameUnaligned(Out, ElTy);
        Out << " x" << i;
        if (isEmptyType(ElTy))
          Out << " */";
        else
          printed = true;
      }
      Out << ") {\n  ";
      printTypeName(Out, *it);
      Out << " r;";
      for (unsigned i = 0; i != e; ++i) {
        Type *ElTy =
            STy ? STy->getElementType(i) : (*it)->getSequentialElementType();
        if (isEmptyType(ElTy))
          continue;
        if (STy)
          Out << "\n  r.field" << i << " = x" << i << ";";
        else if (ATy)
          Out << "\n  r.array[" << i << "] = x" << i << ";";
        else
          cwriter_assert(0);
      }
      Out << "\n  return r;\n}\n";
    }
  }

  // Emit definitions of the intrinsics.
  for (SmallVector<Function *, 16>::iterator I = intrinsicsToDefine.begin(),
                                             E = intrinsicsToDefine.end();
       I != E; ++I) {
    printIntrinsicDefinition(**I, Out);
  }

  if (!M.empty())
    Out << "\n\n/* Function Bodies */\n";

  generateCompilerSpecificCode(OutHeaders, TD);

  if (FCmpOps.erase(FCmpInst::FCMP_ORD)) {
    defineFCmpOp(OutHeaders, FCmpInst::FCMP_ORD);
  }
  if (FCmpOps.erase(FCmpInst::FCMP_UNO)) {
    defineFCmpOp(OutHeaders, FCmpInst::FCMP_UNO);
  }
  for (auto Pred : FCmpOps) {
    defineFCmpOp(OutHeaders, Pred);
  }
  FCmpOps.clear();
}

void CWriter::declareOneGlobalVariable(GlobalVariable *I) {
  if (I->isDeclaration() || isEmptyType(I->getType()->getPointerElementType()))
    return;

  // Ignore special globals, such as debug info.
  if (getGlobalVariableClass(&*I))
    return;

  if (I->hasDLLImportStorageClass())
    Out << "__declspec(dllimport) ";
  else if (I->hasDLLExportStorageClass())
    Out << "__declspec(dllexport) ";

  if (I->hasLocalLinkage())
    Out << "static ";

  // Thread Local Storage
  if (I->isThreadLocal())
    Out << "__thread ";

  Type *ElTy = I->getType()->getElementType();
  unsigned Alignment = I->getAlignment();
  bool IsOveraligned = Alignment && Alignment > TD->getABITypeAlignment(ElTy);

  printTypeName(Out, ElTy, false) << ' ' << GetValueName(I);
  if (IsOveraligned)
    Out << " __attribute__((aligned(" << Alignment << ")))";

  // If the initializer is not null, emit the initializer.  If it is null,
  // we try to avoid emitting large amounts of zeros.  The problem with
  // this, however, occurs when the variable has weak linkage.  In this
  // case, the assembler will complain about the variable being both weak
  // and common, so we disable this optimization.
  // FIXME common linkage should avoid this problem.
  if (!I->getInitializer()->isNullValue()) {
    Out << " = ";
    writeOperand(I->getInitializer(), ContextStatic);
  } else if (I->hasWeakLinkage()) {
    // We have to specify an initializer, but it doesn't have to be
    // complete.  If the value is an aggregate, print out { 0 }, and let
    // the compiler figure out the rest of the zeros.
    Out << " = ";
    if (I->getInitializer()->getType()->isStructTy() ||
        I->getInitializer()->getType()->isVectorTy()) {
      Out << "{ 0 }";
    } else if (I->getInitializer()->getType()->isArrayTy()) {
      // As with structs and vectors, but with an extra set of braces
      // because arrays are wrapped in structs.
      Out << "{ { 0 } }";
    } else {
      // Just print it out normally.
      writeOperand(I->getInitializer(), ContextStatic);
    }
  }
  Out << ";\n";
}

/// Output all floating point constants that cannot be printed accurately...
void CWriter::printFloatingPointConstants(Function &F) {
  // Scan the module for floating point constants.  If any FP constant is used
  // in the function, we want to redirect it here so that we do not depend on
  // the precision of the printed form, unless the printed form preserves
  // precision.
  for (inst_iterator I = inst_begin(&F), E = inst_end(&F); I != E; ++I)
    for (Instruction::op_iterator I_Op = I->op_begin(), E_Op = I->op_end();
         I_Op != E_Op; ++I_Op)
      if (const Constant *C = dyn_cast<Constant>(I_Op))
        printFloatingPointConstants(C);
  Out << '\n';
}

void CWriter::printFloatingPointConstants(const Constant *C) {
  // If this is a constant expression, recursively check for constant fp values.
  if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(C)) {
    for (unsigned i = 0, e = CE->getNumOperands(); i != e; ++i)
      printFloatingPointConstants(CE->getOperand(i));
    return;
  }

  // Otherwise, check for a FP constant that we need to print.
  const ConstantFP *FPC = dyn_cast<ConstantFP>(C);
  if (FPC == nullptr ||
      // Do not put in FPConstantMap if safe.
      isFPCSafeToPrint(FPC) ||
      // Already printed this constant?
      FPConstantMap.has(FPC))
    return;

  unsigned Counter = FPConstantMap.getOrInsert(FPC);

  if (FPC->getType() == Type::getDoubleTy(FPC->getContext())) {
    double Val = FPC->getValueAPF().convertToDouble();
    uint64_t i = FPC->getValueAPF().bitcastToAPInt().getZExtValue();
    headerUseConstantDoubleTy();
    Out << "static const ConstantDoubleTy FPConstant" << Counter << " = 0x"
        << utohexstr(i) << "ULL;    /* " << Val << " */\n";
  } else if (FPC->getType() == Type::getFloatTy(FPC->getContext())) {
    float Val = FPC->getValueAPF().convertToFloat();
    uint32_t i = (uint32_t)FPC->getValueAPF().bitcastToAPInt().getZExtValue();
    headerUseConstantFloatTy();
    Out << "static const ConstantFloatTy FPConstant" << Counter << " = 0x"
        << utohexstr(i) << "U;    /* " << Val << " */\n";
  } else {
    errorWithMessage("Unknown float type!");
  }
}

static void defineBitCastUnion(raw_ostream &Out) {
  Out << "/* Helper union for bitcasts */\n";
  Out << "typedef union {\n";
  Out << "  uint Int32;\n";
  Out << "  ulong Int64;\n";
  Out << "  float Float;\n";
  Out << "  double Double;\n";
  Out << "} llvmBitCastUnion;\n";
}

/// printSymbolTable - Run through symbol table looking for type names.  If a
/// type name is found, emit its declaration...
void CWriter::printModuleTypes(raw_ostream &Out) {
  if (headerIncBitCastUnion()) {
    defineBitCastUnion(Out);
  }

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
  /// isStructReturn - Should this function actually return a struct by-value?
  bool isStructReturn = F.hasStructRetAttr();

  cwriter_assert(!F.isDeclaration());
  if (F.hasDLLImportStorageClass())
    Out << "__declspec(dllimport) ";
  if (F.hasDLLExportStorageClass())
    Out << "__declspec(dllexport) ";
  if (F.hasLocalLinkage())
    Out << "static ";

  iterator_range<Function::arg_iterator> args = F.args();
  printFunctionProto(Out, F.getFunctionType(),
                     std::make_pair(F.getAttributes(), F.getCallingConv()),
                     GetValueName(&F), &args);

  Out << " {\n";

  // If this is a struct return function, handle the result with magic.
  if (isStructReturn) {
    Type *StructTy =
        cast<PointerType>(F.arg_begin()->getType())->getElementType();
    Out << "  ";
    printTypeName(Out, StructTy, false)
        << " StructReturn;  /* Struct return temporary */\n";

    Out << "  ";
    printTypeName(Out, F.arg_begin()->getType(), false);
    Out << GetValueName(F.arg_begin()) << " = &StructReturn;\n";
  }

  bool PrintedVar = false;

  // print local variable information for the function
  for (inst_iterator I = inst_begin(&F), E = inst_end(&F); I != E; ++I) {
    if (AllocaInst *AI = isDirectAlloca(&*I)) {
      unsigned Alignment = AI->getAlignment();
      bool IsOveraligned = Alignment && Alignment > TD->getABITypeAlignment(
                                                        AI->getAllocatedType());
      Out << "  ";

      printTypeName(Out, AI->getAllocatedType(), false) << ' ';
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
            << ' ' << (GetValueName(&*I) + "__PHI_TEMPORARY");
        Out << ";\n";
      }
      PrintedVar = true;
    }
    // We need a temporary for the BitCast to use so it can pluck a value out
    // of a union to do the BitCast. This is separate from the need for a
    // variable to hold the result of the BitCast.
    if (isFPIntBitCast(*I)) {
      headerUseBitCastUnion();
      Out << "  llvmBitCastUnion " << GetValueName(&*I)
          << "__BITCAST_TEMPORARY;\n";
      PrintedVar = true;
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
    if (!isInlinableInst(*II) && !isDirectAlloca(&*II)) {
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
  bool isStructReturn = I.getParent()->getParent()->hasStructRetAttr();

  if (isStructReturn) {
    Out << "  return StructReturn;\n";
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
    writeOperand(I.getOperand(0), ContextCasted);
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

void CWriter::visitIndirectBrInst(IndirectBrInst &IBI) {
  CurInstr = &IBI;

  Out << "  goto *(void*)(";
  writeOperand(IBI.getOperand(0));
  Out << ");\n";
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
      Out << "  " << GetValueName(&*I) << "__PHI_TEMPORARY = ";
      writeOperand(IV, ContextCasted);
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
      writeOperand(I.getCondition(), ContextCasted);
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
      writeOperand(I.getCondition(), ContextCasted);
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
  Out << "__PHI_TEMPORARY";
}

void CWriter::visitBinaryOperator(BinaryOperator &I) {
  using namespace PatternMatch;

  CurInstr = &I;

  // binary instructions, shift instructions, setCond instructions.
  cwriter_assert(!I.getType()->isPointerTy());

  // We must cast the results of binary operations which might be promoted.
  bool needsCast = false;
  if ((I.getType() == Type::getInt8Ty(I.getContext())) ||
      (I.getType() == Type::getInt16Ty(I.getContext())) ||
      (I.getType() == Type::getFloatTy(I.getContext()))) {
    // types too small to work with directly
    needsCast = true;
  } else if (I.getType()->getPrimitiveSizeInBits() > 64) {
    // types too big to work with directly
    needsCast = true;
  }
  bool shouldCast;
  bool castIsSigned;
  opcodeNeedsCast(I.getOpcode(), shouldCast, castIsSigned);

  if (I.getType()->isVectorTy() || needsCast || shouldCast) {
    Type *VTy = I.getOperand(0)->getType();
    unsigned opcode;
    Value *X;
    if (match(&I, m_Neg(m_Value(X)))) {
      opcode = BinaryNeg;
      Out << "llvm_neg_";
      printTypeString(Out, VTy, false);
      Out << "(";
      writeOperand(X, ContextCasted);
    } else if (match(&I, m_FNeg(m_Value(X)))) {
      opcode = BinaryNeg;
      Out << "llvm_neg_";
      printTypeString(Out, VTy, false);
      Out << "(";
      writeOperand(X, ContextCasted);
    } else if (match(&I, m_Not(m_Value(X)))) {
      opcode = BinaryNot;
      Out << "llvm_not_";
      printTypeString(Out, VTy, false);
      Out << "(";
      writeOperand(X, ContextCasted);
    } else {
      opcode = I.getOpcode();
      Out << "llvm_" << Instruction::getOpcodeName(opcode) << "_";
      printTypeString(Out, VTy, false);
      Out << "(";
      writeOperand(I.getOperand(0), ContextCasted);
      Out << ", ";
      writeOperand(I.getOperand(1), ContextCasted);
    }
    Out << ")";
    InlineOpDeclTypes.insert(std::pair<unsigned, Type *>(opcode, VTy));
    return;
  }

  // If this is a negation operation, print it out as such.  For FP, we don't
  // want to print "-0.0 - X".
  Value *X;
  if (match(&I, m_Neg(m_Value(X)))) {
    Out << "-(";
    writeOperand(X);
    Out << ")";
  } else if (match(&I, m_FNeg(m_Value(X)))) {
    Out << "-(";
    writeOperand(X);
    Out << ")";
  } else if (match(&I, m_Not(m_Value(X)))) {
    Out << "~(";
    writeOperand(X);
    Out << ")";
  } else if (I.getOpcode() == Instruction::FRem) {
    // Output a call to fmod/fmodf instead of emitting a%b
    if (I.getType() == Type::getFloatTy(I.getContext()))
      Out << "fmodf(";
    else if (I.getType() == Type::getDoubleTy(I.getContext()))
      Out << "fmod(";
    else // all 3 flavors of long double
      Out << "fmodl(";
    writeOperand(I.getOperand(0), ContextCasted);
    Out << ", ";
    writeOperand(I.getOperand(1), ContextCasted);
    Out << ")";
  } else {

    // Write out the cast of the instruction's value back to the proper type
    // if necessary.
    bool NeedsClosingParens = writeInstructionCast(I);

    // Certain instructions require the operand to be forced to a specific type
    // so we use writeOperandWithCast here instead of writeOperand. Similarly
    // below for operand 1
    writeOperandWithCast(I.getOperand(0), I.getOpcode());

    switch (I.getOpcode()) {
    case Instruction::Add:
    case Instruction::FAdd:
      Out << " + ";
      break;
    case Instruction::Sub:
    case Instruction::FSub:
      Out << " - ";
      break;
    case Instruction::Mul:
    case Instruction::FMul:
      Out << " * ";
      break;
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
      Out << " % ";
      break;
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::FDiv:
      Out << " / ";
      break;
    case Instruction::And:
      Out << " & ";
      break;
    case Instruction::Or:
      Out << " | ";
      break;
    case Instruction::Xor:
      Out << " ^ ";
      break;
    case Instruction::Shl:
      Out << " << ";
      break;
    case Instruction::LShr:
    case Instruction::AShr:
      Out << " >> ";
      break;
    default:
#ifndef NDEBUG
      errs() << "Invalid operator type!" << I << "\n";
#endif
      errorWithMessage("invalid operator type");
    }

    writeOperandWithCast(I.getOperand(1), I.getOpcode());
    if (NeedsClosingParens)
      Out << "))";
  }
}

void CWriter::visitICmpInst(ICmpInst &I) {
  CurInstr = &I;

  if (I.getType()->isVectorTy() ||
      I.getOperand(0)->getType()->getPrimitiveSizeInBits() > 64) {
    Out << "llvm_icmp_" << getCmpPredicateName(I.getPredicate()) << "_";
    printTypeString(Out, I.getOperand(0)->getType(), I.isSigned());
    Out << "(";
    writeOperand(I.getOperand(0), ContextCasted);
    Out << ", ";
    writeOperand(I.getOperand(1), ContextCasted);
    Out << ")";
    if (VectorType *VTy = dyn_cast<VectorType>(I.getOperand(0)->getType())) {
      CmpDeclTypes.insert(
          std::pair<CmpInst::Predicate, VectorType *>(I.getPredicate(), VTy));
      TypedefDeclTypes.insert(
          I.getType()); // insert type not necessarily visible above
    }
    return;
  }

  // Write out the cast of the instruction's value back to the proper type
  // if necessary.
  bool NeedsClosingParens = writeInstructionCast(I);

  // Certain icmp predicate require the operand to be forced to a specific type
  // so we use writeOperandWithCast here instead of writeOperand. Similarly
  // below for operand 1
  writeOperandWithCast(I.getOperand(0), I);

  switch (I.getPredicate()) {
  case ICmpInst::ICMP_EQ:
    Out << " == ";
    break;
  case ICmpInst::ICMP_NE:
    Out << " != ";
    break;
  case ICmpInst::ICMP_ULE:
  case ICmpInst::ICMP_SLE:
    Out << " <= ";
    break;
  case ICmpInst::ICMP_UGE:
  case ICmpInst::ICMP_SGE:
    Out << " >= ";
    break;
  case ICmpInst::ICMP_ULT:
  case ICmpInst::ICMP_SLT:
    Out << " < ";
    break;
  case ICmpInst::ICMP_UGT:
  case ICmpInst::ICMP_SGT:
    Out << " > ";
    break;
  default:
#ifndef NDEBUG
    errs() << "Invalid icmp predicate!" << I << "\n";
#endif
    errorWithMessage("invalid icmp predicate");
  }

  writeOperandWithCast(I.getOperand(1), I);
  if (NeedsClosingParens)
    Out << "))";
}

void CWriter::visitFCmpInst(FCmpInst &I) {
  CurInstr = &I;

  if (I.getType()->isVectorTy()) {
    Out << "llvm_fcmp_" << getCmpPredicateName(I.getPredicate()) << "_";
    printTypeString(Out, I.getOperand(0)->getType(), I.isSigned());
    Out << "(";
    writeOperand(I.getOperand(0), ContextCasted);
    Out << ", ";
    writeOperand(I.getOperand(1), ContextCasted);
    Out << ")";
    if (VectorType *VTy = dyn_cast<VectorType>(I.getOperand(0)->getType())) {
      CmpDeclTypes.insert(
          std::pair<CmpInst::Predicate, VectorType *>(I.getPredicate(), VTy));
      TypedefDeclTypes.insert(
          I.getType()); // insert type not necessarily visible above
    }
    return;
  }

  const auto Pred = I.getPredicate();
  headerUseFCmpOp(Pred);
  Out << "llvm_fcmp_" << getCmpPredicateName(Pred) << "(";
  // Write the first operand
  writeOperand(I.getOperand(0), ContextCasted);
  Out << ", ";
  // Write the second operand
  writeOperand(I.getOperand(1), ContextCasted);
  Out << ")";
}

static const char *getFloatBitCastField(Type *Ty) {
  switch (Ty->getTypeID()) {
  default:
    llvm_unreachable("Invalid Type");
  case Type::FloatTyID:
    return "Float";
  case Type::DoubleTyID:
    return "Double";
  case Type::IntegerTyID: {
    unsigned NumBits = cast<IntegerType>(Ty)->getBitWidth();
    if (NumBits <= 32)
      return "Int32";
    else
      return "Int64";
  }
  }
}

void CWriter::visitCastInst(CastInst &I) {
  CurInstr = &I;

  Type *DstTy = I.getType();
  Type *SrcTy = I.getOperand(0)->getType();

  if (DstTy->isVectorTy() || SrcTy->isVectorTy() ||
      DstTy->getPrimitiveSizeInBits() > 64 ||
      SrcTy->getPrimitiveSizeInBits() > 64) {
    Out << "llvm_" << I.getOpcodeName() << "_";
    printTypeString(Out, SrcTy, false);
    Out << "_";
    printTypeString(Out, DstTy, false);
    Out << "(";
    writeOperand(I.getOperand(0), ContextCasted);
    Out << ")";
    CastOpDeclTypes.insert(
        std::pair<Instruction::CastOps, std::pair<Type *, Type *>>(
            I.getOpcode(), std::pair<Type *, Type *>(SrcTy, DstTy)));
    return;
  }

  if (isFPIntBitCast(I)) {
    Out << '(';
    // These int<->float and long<->double casts need to be handled specially
    Out << GetValueName(&I) << "__BITCAST_TEMPORARY."
        << getFloatBitCastField(I.getOperand(0)->getType()) << " = ";
    writeOperand(I.getOperand(0), ContextCasted);
    Out << ", " << GetValueName(&I) << "__BITCAST_TEMPORARY."
        << getFloatBitCastField(I.getType());
    Out << ')';
    return;
  }

  Out << '(';
  printCast(I.getOpcode(), SrcTy, DstTy);

  // Make a sext from i1 work by subtracting the i1 from 0 (an int).
  if (SrcTy == Type::getInt1Ty(I.getContext()) &&
      I.getOpcode() == Instruction::SExt)
    Out << "0-";

  writeOperand(I.getOperand(0), ContextCasted);

  if (DstTy == Type::getInt1Ty(I.getContext()) &&
      (I.getOpcode() == Instruction::Trunc ||
       I.getOpcode() == Instruction::FPToUI ||
       I.getOpcode() == Instruction::FPToSI ||
       I.getOpcode() == Instruction::PtrToInt)) {
    // Make sure we really get a trunc to bool by anding the operand with 1
    Out << "&1u";
  }
  Out << ')';
}

void CWriter::visitSelectInst(SelectInst &I) {
  CurInstr = &I;

  Out << "llvm_select_";
  printTypeString(Out, I.getType(), false);
  Out << "(";
  writeOperand(I.getCondition(), ContextCasted);
  Out << ", ";
  writeOperand(I.getTrueValue(), ContextCasted);
  Out << ", ";
  writeOperand(I.getFalseValue(), ContextCasted);
  Out << ")";
  SelectDeclTypes.insert(I.getType());
  cwriter_assert(
      I.getCondition()->getType()->isVectorTy() ==
      I.getType()->isVectorTy()); // TODO: might be scalarty == vectorty
}

// Returns the macro name or value of the max or min of an integer type
// (as defined in limits.h).
static void printLimitValue(IntegerType &Ty, bool isSigned, bool isMax,
                            raw_ostream &Out) {
  const char *type;
  const char *sprefix = "";

  unsigned NumBits = Ty.getBitWidth();
  if (NumBits <= 8) {
    type = "CHAR";
    sprefix = "S";
  } else if (NumBits <= 16) {
    type = "SHRT";
  } else if (NumBits <= 32) {
    type = "INT";
  } else if (NumBits <= 64) {
    type = "LLONG";
  } else {
    llvm_unreachable("Bit widths > 64 not implemented yet");
  }

  if (isSigned)
    Out << sprefix << type << (isMax ? "_MAX" : "_MIN");
  else
    Out << "U" << type << (isMax ? "_MAX" : "0");
}

#ifndef NDEBUG
static bool isSupportedIntegerSize(IntegerType &T) {
  return T.getBitWidth() == 8 || T.getBitWidth() == 16 ||
         T.getBitWidth() == 32 || T.getBitWidth() == 64 ||
         T.getBitWidth() == 128;
}
#endif

void CWriter::printIntrinsicDefinition(FunctionType *funT, unsigned Opcode,
                                       std::string OpName, raw_ostream &Out) {
  Type *retT = funT->getReturnType();
  Type *elemT = funT->getParamType(0);
  IntegerType *elemIntT = dyn_cast<IntegerType>(elemT);
  char i, numParams = funT->getNumParams();
  bool isSigned;
  switch (Opcode) {
  default:
    isSigned = false;
    break;
  case Intrinsic::sadd_with_overflow:
  case Intrinsic::ssub_with_overflow:
  case Intrinsic::smul_with_overflow:
    isSigned = true;
    break;
  }
  cwriter_assert(numParams > 0 && numParams < 26);

  if (isa<VectorType>(retT)) {
    // this looks general, but is only actually used for ctpop, ctlz, cttz
    Type **devecFunParams = (Type **)alloca(sizeof(Type *) * numParams);
    for (i = 0; i < numParams; i++) {
      devecFunParams[(int)i] = funT->params()[(int)i]->getScalarType();
    }
    FunctionType *devecFunT = FunctionType::get(
        funT->getReturnType()->getScalarType(),
        makeArrayRef(devecFunParams, numParams), funT->isVarArg());
    printIntrinsicDefinition(devecFunT, Opcode, OpName + "_devec", Out);
  }

  // static Rty _llvm_op_ixx(unsigned ixx a, unsigned ixx b) {
  //   Rty r;
  //   <opcode here>
  //   return r;
  // }
  Out << "static ";
  printTypeName(Out, retT);
  Out << " ";
  Out << OpName;
  Out << "(";
  for (i = 0; i < numParams; i++) {
    switch (Opcode) {
    // optional intrinsic validity cwriter_assertion checks
    default:
      // default case: assume all parameters must have the same type
      cwriter_assert(elemT == funT->getParamType(i));
      break;
    case Intrinsic::ctlz:
    case Intrinsic::cttz:
    case Intrinsic::powi:
      break;
    }
    printTypeNameUnaligned(Out, funT->getParamType(i), isSigned);
    Out << " " << (char)('a' + i);
    if (i != numParams - 1)
      Out << ", ";
  }
  Out << ") {\n  ";
  printTypeName(Out, retT);
  Out << " r;\n";

  if (isa<VectorType>(retT)) {
    for (i = 0; i < numParams; i++) {
      Out << "  r.";
      printVectorComponent(Out, (int)i);
      Out << OpName << "_devec(";
      for (char j = 0; j < numParams; j++) {
        Out << (char)('a' + j);
        if (isa<VectorType>(funT->params()[j]))
          Out << ".";
          printVectorComponent(Out, (int)i);
        if (j != numParams - 1)
          Out << ", ";
      }
      Out << ");\n";
    }
  } else if (elemIntT) {
    // handle integer ops
    cwriter_assert(isSupportedIntegerSize(*elemIntT) &&
                   "CBackend does not support arbitrary size integers.");
    switch (Opcode) {
    default:
#ifndef NDEBUG
      errs() << "Unsupported Intrinsic!" << Opcode << "\n";
#endif
      errorWithMessage("unsupported instrinsic");

    case Intrinsic::uadd_with_overflow:
      //   r.field0 = a + b;
      //   r.field1 = (r.field0 < a);
      cwriter_assert(cast<StructType>(retT)->getElementType(0) == elemT);
      Out << "  r.field0 = a + b;\n";
      Out << "  r.field1 = (a >= -b);\n";
      break;

    case Intrinsic::sadd_with_overflow:
      //   r.field0 = a + b;
      //   r.field1 = (b > 0 && a > XX_MAX - b) ||
      //              (b < 0 && a < XX_MIN - b);
      cwriter_assert(cast<StructType>(retT)->getElementType(0) == elemT);
      Out << "  r.field0 = a + b;\n";
      Out << "  r.field1 = (b >= 0 ? a > ";
      printLimitValue(*elemIntT, true, true, Out);
      Out << " - b : a < ";
      printLimitValue(*elemIntT, true, false, Out);
      Out << " - b);\n";
      break;

    case Intrinsic::usub_with_overflow:
      cwriter_assert(cast<StructType>(retT)->getElementType(0) == elemT);
      Out << "  r.field0 = a - b;\n";
      Out << "  r.field1 = (a < b);\n";
      break;

    case Intrinsic::ssub_with_overflow:
      cwriter_assert(cast<StructType>(retT)->getElementType(0) == elemT);
      Out << "  r.field0 = a - b;\n";
      Out << "  r.field1 = (b <= 0 ? a > ";
      printLimitValue(*elemIntT, true, true, Out);
      Out << " + b : a < ";
      printLimitValue(*elemIntT, true, false, Out);
      Out << " + b);\n";
      break;

    case Intrinsic::umul_with_overflow:
      cwriter_assert(cast<StructType>(retT)->getElementType(0) == elemT);
      Out << "  r.field1 = LLVMMul_uov(8 * sizeof(a), &a, &b, &r.field0);\n";
      break;

    case Intrinsic::smul_with_overflow:
      cwriter_assert(cast<StructType>(retT)->getElementType(0) == elemT);
      Out << "  r.field1 = LLVMMul_sov(8 * sizeof(a), &a, &b, &r.field0);\n";
      break;

    case Intrinsic::bswap:
      cwriter_assert(retT == elemT);
      Out << "  LLVMFlipAllBits(8 * sizeof(a), &a, &r);\n";
      break;

    case Intrinsic::ctpop:
      cwriter_assert(retT == elemT);
      Out << "  r = ";
      if (retT->getPrimitiveSizeInBits() > 64)
        Out << "llvm_ctor_u128(0, ";
      Out << "LLVMCountPopulation(8 * sizeof(a), &a)";
      if (retT->getPrimitiveSizeInBits() > 64)
        Out << ")";
      Out << ";\n";
      break;

    case Intrinsic::ctlz:
      cwriter_assert(retT == elemT);
      Out << "  (void)b;\n  r = ";
      if (retT->getPrimitiveSizeInBits() > 64)
        Out << "llvm_ctor_u128(0, ";
      Out << "LLVMCountLeadingZeros(8 * sizeof(a), &a)";
      if (retT->getPrimitiveSizeInBits() > 64)
        Out << ")";
      Out << ";\n";
      break;

    case Intrinsic::cttz:
      cwriter_assert(retT == elemT);
      Out << "  (void)b;\n  r = ";
      if (retT->getPrimitiveSizeInBits() > 64)
        Out << "llvm_ctor_u128(0, ";
      Out << "LLVMCountTrailingZeros(8 * sizeof(a), &a)";
      if (retT->getPrimitiveSizeInBits() > 64)
        Out << ")";
      Out << ";\n";
      break;
    }

  } else {
    // handle FP ops
    const char *suffix;
    cwriter_assert(retT == elemT);
    if (elemT->isFloatTy() || elemT->isHalfTy()) {
      suffix = "f";
    } else if (elemT->isDoubleTy()) {
      suffix = "";
    } else if (elemT->isFP128Ty()) {
    } else if (elemT->isX86_FP80Ty()) {
    } else if (elemT->isPPC_FP128Ty()) {
      suffix = "l";
    } else {
#ifndef NDEBUG
      errs() << "Unsupported Intrinsic!" << Opcode << "\n";
#endif
      errorWithMessage("unsupported instrinsic");
    }

    switch (Opcode) {
    default:
#ifndef NDEBUG
      errs() << "Unsupported Intrinsic!" << Opcode << "\n";
#endif
      errorWithMessage("unsupported instrinsic");

    case Intrinsic::ceil:
      Out << "  r = ceil" << suffix << "(a);\n";
      break;

    case Intrinsic::fabs:
      Out << "  r = fabs" << suffix << "(a);\n";
      break;

    case Intrinsic::floor:
      Out << "  r = floor" << suffix << "(a);\n";
      break;

    case Intrinsic::fma:
      Out << "  r = fma" << suffix << "(a, b, c);\n";
      break;

    case Intrinsic::fmuladd:
      Out << "  r = a * b + c;\n";
      break;

    case Intrinsic::pow:
    case Intrinsic::powi:
      Out << "  r = pow" << suffix << "(a, b);\n";
      break;

    case Intrinsic::rint:
      Out << "  r = rint" << suffix << "(a);\n";
      break;

    case Intrinsic::sqrt:
      Out << "  r = sqrt" << suffix << "(a);\n";
      break;

    case Intrinsic::trunc:
      Out << "  r = trunc" << suffix << "(a);\n";
      break;
    }
  }

  Out << "  return r;\n}\n";
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
          switch (F->getIntrinsicID()) {
          case Intrinsic::not_intrinsic:
          case Intrinsic::vastart:
          case Intrinsic::vacopy:
          case Intrinsic::vaend:
          case Intrinsic::returnaddress:
          case Intrinsic::frameaddress:
          case Intrinsic::setjmp:
          case Intrinsic::longjmp:
          case Intrinsic::sigsetjmp:
          case Intrinsic::siglongjmp:
          case Intrinsic::prefetch:
          case Intrinsic::x86_sse_cmp_ss:
          case Intrinsic::x86_sse_cmp_ps:
          case Intrinsic::x86_sse2_cmp_sd:
          case Intrinsic::x86_sse2_cmp_pd:
          case Intrinsic::ppc_altivec_lvsl:
          case Intrinsic::uadd_with_overflow:
          case Intrinsic::sadd_with_overflow:
          case Intrinsic::usub_with_overflow:
          case Intrinsic::ssub_with_overflow:
          case Intrinsic::umul_with_overflow:
          case Intrinsic::smul_with_overflow:
          case Intrinsic::bswap:
          case Intrinsic::ceil:
          case Intrinsic::ctlz:
          case Intrinsic::ctpop:
          case Intrinsic::cttz:
          case Intrinsic::fabs:
          case Intrinsic::floor:
          case Intrinsic::fma:
          case Intrinsic::fmuladd:
          case Intrinsic::pow:
          case Intrinsic::powi:
          case Intrinsic::rint:
          case Intrinsic::sqrt:
          case Intrinsic::trunc:
          case Intrinsic::trap:
          case Intrinsic::stackprotector:
          case Intrinsic::dbg_value:
          case Intrinsic::dbg_declare:
            // We directly implement these intrinsics
            break;

          default:
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

            break;
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
  bool hasByVal = I.hasByValArgument();
  bool isStructRet = I.hasStructRetAttr();
  if (isStructRet) {
    writeOperandDeref(I.getArgOperand(0));
    Out << " = ";
  }

  if (I.isTailCall())
    Out << " /*tail*/ ";

  // If this is an indirect call to a struct return function, we need to cast
  // the pointer. Ditto for indirect calls with byval arguments.
  bool NeedsCast =
      (hasByVal || isStructRet || I.getCallingConv() != CallingConv::C) &&
      !isa<Function>(Callee);

  // GCC is a real PITA.  It does not permit codegening casts of functions to
  // function pointers if they are in a call (it generates a trap instruction
  // instead!).  We work around this by inserting a cast to void* in between
  // the function and the function pointer cast.  Unfortunately, we can't just
  // form the constant expression here, because the folder will immediately
  // nuke it.
  //
  // Note finally, that this is completely unsafe.  ANSI C does not guarantee
  // that void* and function pointers have the same size. :( To deal with this
  // in the common case, we handle casts where the number of arguments passed
  // match exactly.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Callee))
    if (CE->isCast())
      if (Function *RF = dyn_cast<Function>(CE->getOperand(0))) {
        NeedsCast = true;
        Callee = RF;
      }

  if (NeedsCast) {
    // Ok, just cast the pointer type.
    Out << "((";
    printTypeName(Out, I.getCalledValue()->getType()->getPointerElementType(),
                  false, std::make_pair(PAL, I.getCallingConv()));
    Out << "*)(void*)";
  }
  writeOperand(Callee, ContextCasted);
  if (NeedsCast)
    Out << ')';

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

  Function *F = I.getCalledFunction();
  if (F) {
    StringRef Name = F->getName();
    // emit cast for the first argument to type expected by header prototype
    // the jmp_buf type is an array, so the array-to-pointer decay adds the
    // strange extra *'s
    if (Name == "sigsetjmp")
      Out << "*(sigjmp_buf*)";
    else if (Name == "setjmp")
      Out << "*(jmp_buf*)";
  }

  for (; AI != AE; ++AI, ++ArgNo) {
    if (PrintedArg)
      Out << ", ";
    if (ArgNo < NumDeclaredParams &&
        (*AI)->getType() != FTy->getParamType(ArgNo)) {
      Out << '(';
      printTypeNameUnaligned(
          Out, FTy->getParamType(ArgNo),
          /*isSigned=*/PAL.hasAttribute(ArgNo + 1, Attribute::SExt));
      Out << ')';
    }
    // Check if the argument is expected to be passed by value.
    if (I.getAttributes().hasAttribute(ArgNo + 1, Attribute::ByVal))
      writeOperandDeref(*AI);
    else
      writeOperand(*AI, ContextCasted);
    PrintedArg = true;
  }
  Out << ')';
}

/// visitBuiltinCall - Handle the call to the specified builtin.  Returns true
/// if the entire call is handled, return false if it wasn't handled
bool CWriter::visitBuiltinCall(CallInst &I, Intrinsic::ID ID) {
  CurInstr = &I;

  switch (ID) {
  default: {
#ifndef NDEBUG
    errs() << "Unknown LLVM intrinsic! " << I << "\n";
#endif
    errorWithMessage("unknown llvm instrinsic");
    return false;
  }
  case Intrinsic::dbg_value:
  case Intrinsic::dbg_declare:
    return true; // ignore these intrinsics
  case Intrinsic::vastart:
    Out << "0; ";

    Out << "va_start(*(va_list*)";
    writeOperand(I.getArgOperand(0), ContextCasted);
    Out << ", ";
    // Output the last argument to the enclosing function.
    if (I.getParent()->getParent()->arg_empty())
      Out << "vararg_dummy_arg";
    else {
      Function::arg_iterator arg_end = I.getParent()->getParent()->arg_end();
      writeOperand(--arg_end);
    }
    Out << ')';
    return true;
  case Intrinsic::vaend:
    if (!isa<ConstantPointerNull>(I.getArgOperand(0))) {
      Out << "0; va_end(*(va_list*)";
      writeOperand(I.getArgOperand(0), ContextCasted);
      Out << ')';
    } else {
      Out << "va_end(*(va_list*)0)";
    }
    return true;
  case Intrinsic::vacopy:
    Out << "0; ";
    Out << "va_copy(*(va_list*)";
    writeOperand(I.getArgOperand(0), ContextCasted);
    Out << ", *(va_list*)";
    writeOperand(I.getArgOperand(1), ContextCasted);
    Out << ')';
    return true;
  case Intrinsic::returnaddress:
    Out << "__builtin_return_address(";
    writeOperand(I.getArgOperand(0), ContextCasted);
    Out << ')';
    return true;
  case Intrinsic::frameaddress:
    Out << "__builtin_frame_address(";
    writeOperand(I.getArgOperand(0), ContextCasted);
    Out << ')';
    return true;
  case Intrinsic::setjmp:
    Out << "setjmp(*(jmp_buf*)";
    writeOperand(I.getArgOperand(0), ContextCasted);
    Out << ')';
    return true;
  case Intrinsic::longjmp:
    Out << "longjmp(*(jmp_buf*)";
    writeOperand(I.getArgOperand(0), ContextCasted);
    Out << ", ";
    writeOperand(I.getArgOperand(1), ContextCasted);
    Out << ')';
    return true;
  case Intrinsic::sigsetjmp:
    Out << "sigsetjmp(*(sigjmp_buf*)";
    writeOperand(I.getArgOperand(0), ContextCasted);
    Out << ',';
    writeOperand(I.getArgOperand(1), ContextCasted);
    Out << ')';
    return true;
  case Intrinsic::siglongjmp:
    Out << "siglongjmp(*(sigjmp_buf*)";
    writeOperand(I.getArgOperand(0), ContextCasted);
    Out << ", ";
    writeOperand(I.getArgOperand(1), ContextCasted);
    Out << ')';
    return true;
  case Intrinsic::prefetch:
    Out << "LLVM_PREFETCH((const void *)";
    writeOperand(I.getArgOperand(0), ContextCasted);
    Out << ", ";
    writeOperand(I.getArgOperand(1), ContextCasted);
    Out << ", ";
    writeOperand(I.getArgOperand(2), ContextCasted);
    Out << ")";
    return true;
  case Intrinsic::stacksave:
    return true;
  case Intrinsic::x86_sse_cmp_ss:
  case Intrinsic::x86_sse_cmp_ps:
  case Intrinsic::x86_sse2_cmp_sd:
  case Intrinsic::x86_sse2_cmp_pd:
    Out << '(';
    printTypeName(Out, I.getType());
    Out << ')';
    // Multiple GCC builtins multiplex onto this intrinsic.
    switch (cast<ConstantInt>(I.getArgOperand(2))->getZExtValue()) {
    default:
      errorWithMessage("Invalid llvm.x86.sse.cmp!");
    case 0:
      Out << "__builtin_ia32_cmpeq";
      break;
    case 1:
      Out << "__builtin_ia32_cmplt";
      break;
    case 2:
      Out << "__builtin_ia32_cmple";
      break;
    case 3:
      Out << "__builtin_ia32_cmpunord";
      break;
    case 4:
      Out << "__builtin_ia32_cmpneq";
      break;
    case 5:
      Out << "__builtin_ia32_cmpnlt";
      break;
    case 6:
      Out << "__builtin_ia32_cmpnle";
      break;
    case 7:
      Out << "__builtin_ia32_cmpord";
      break;
    }
    if (ID == Intrinsic::x86_sse_cmp_ps || ID == Intrinsic::x86_sse2_cmp_pd)
      Out << 'p';
    else
      Out << 's';
    if (ID == Intrinsic::x86_sse_cmp_ss || ID == Intrinsic::x86_sse_cmp_ps)
      Out << 's';
    else
      Out << 'd';

    Out << "(";
    writeOperand(I.getArgOperand(0), ContextCasted);
    Out << ", ";
    writeOperand(I.getArgOperand(1), ContextCasted);
    Out << ")";
    return true;
  case Intrinsic::ppc_altivec_lvsl:
    Out << '(';
    printTypeName(Out, I.getType());
    Out << ')';
    Out << "__builtin_altivec_lvsl(0, (void*)";
    writeOperand(I.getArgOperand(0), ContextCasted);
    Out << ")";
    return true;
  case Intrinsic::stackprotector:
    writeOperandDeref(I.getArgOperand(1));
    Out << " = ";
    writeOperand(I.getArgOperand(0), ContextCasted);
    return true;
  case Intrinsic::uadd_with_overflow:
  case Intrinsic::sadd_with_overflow:
  case Intrinsic::usub_with_overflow:
  case Intrinsic::ssub_with_overflow:
  case Intrinsic::umul_with_overflow:
  case Intrinsic::smul_with_overflow:
  case Intrinsic::bswap:
  case Intrinsic::ceil:
  case Intrinsic::ctlz:
  case Intrinsic::ctpop:
  case Intrinsic::cttz:
  case Intrinsic::fabs:
  case Intrinsic::floor:
  case Intrinsic::fma:
  case Intrinsic::fmuladd:
  case Intrinsic::pow:
  case Intrinsic::powi:
  case Intrinsic::rint:
  case Intrinsic::sqrt:
  case Intrinsic::trap:
  case Intrinsic::trunc:
    return false; // these use the normal function call emission
  }
}

void CWriter::printGEPExpression(Value *Ptr, gep_type_iterator I,
                                 gep_type_iterator E) {

  // If there are no indices, just print out the pointer.
  if (I == E) {
    writeOperand(Ptr);
    return;
  }

  // Find out if the last index is into a vector.  If so, we have to print this
  // specially.  Since vectors can't have elements of indexable type, only the
  // last index could possibly be of a vector element.
  VectorType *LastIndexIsVector = 0;
  {
    for (gep_type_iterator TmpI = I; TmpI != E; ++TmpI)
      LastIndexIsVector = dyn_cast<VectorType>(TmpI.getIndexedType());
  }

  Out << "(";

  // If the last index is into a vector, we can't print it as &a[i][j] because
  // we can't index into a vector with j in GCC.  Instead, emit this as
  // (((float*)&a[i])+j)
  // TODO: this is no longer true now that we don't represent vectors using
  // gcc-extentions
  if (LastIndexIsVector) {
    //Out << "((";
    printTypeName(Out,
                  PointerType::getUnqual(LastIndexIsVector->getElementType()));
    Out << ")(";
  }

  Out << '&';

  Type *IntoT = I.getIndexedType();

  // If the first index is 0 (very typical) we can do a number of
  // simplifications to clean up the code.
  Value *FirstOp = I.getOperand();
  if (!isa<Constant>(FirstOp) || !cast<Constant>(FirstOp)->isNullValue()) {
    // First index isn't simple, print it the hard way.
    writeOperand(Ptr);
  } else {
    IntoT = I.getIndexedType();
    ++I; // Skip the zero index.

    // Okay, emit the first operand. If Ptr is something that is already address
    // exposed, like a global, avoid emitting (&foo)[0], just emit foo instead.
    if (isAddressExposed(Ptr)) {
      writeOperandInternal(Ptr);
    } else if (I != E && I.isStruct()) {
      // If we didn't already emit the first operand, see if we can print it as
      // P->f instead of "P[0].f"
      writeOperand(Ptr);
      Out << "->field" << cast<ConstantInt>(I.getOperand())->getZExtValue();
      IntoT = I.getIndexedType();
      ++I; // eat the struct index as well.
    } else {
      // Instead of emitting P[0][1], emit (*P)[1], which is more idiomatic.
      Out << "(*";
      writeOperand(Ptr);
      Out << ")";
    }
  }

  for (; I != E; ++I) {
    cwriter_assert(
        I.getOperand()
            ->getType()
            ->isIntegerTy()); // TODO: indexing a Vector with a Vector is valid,
                              // but we don't support it here
    if (I.isStruct()) {
      Out << ".field" << cast<ConstantInt>(I.getOperand())->getZExtValue();
    } else if (IntoT->isArrayTy()) {
      Out << ".array[";
      writeOperandWithCast(I.getOperand(), Instruction::GetElementPtr);
      Out << ']';
    } else if (!IntoT->isVectorTy()) {
      Out << '[';
      writeOperandWithCast(I.getOperand(), Instruction::GetElementPtr);
      Out << ']';
    } else {
      // If the last index is into a vector, then print it out as "+j)".  This
      // works with the 'LastIndexIsVector' code above.
      if (isa<Constant>(I.getOperand()) &&
          cast<Constant>(I.getOperand())->isNullValue()) {
        Out << "))"; // avoid "+0".
      } else {
        Out << ")+(";
        writeOperandWithCast(I.getOperand(), Instruction::GetElementPtr);
        Out << "))";
      }
    }

    IntoT = I.getIndexedType();
  }
  Out << ")";
}

void CWriter::writeMemoryAccess(Value *Operand, Type *OperandType,
                                bool IsVolatile, unsigned Alignment /*bytes*/) {
  if (isAddressExposed(Operand) && !IsVolatile) {
    writeOperandInternal(Operand);
    return;
  }

  bool IsUnaligned =
      Alignment && Alignment < TD->getABITypeAlignment(OperandType);

  if (!IsUnaligned) {
    Out << '*';
    if (IsVolatile) {
      Out << "(volatile ";
      printTypeName(Out, OperandType, false);
      Out << "*)";
    }
  } else if (IsUnaligned) {
    headerUseUnalignedLoad();
    Out << "__UNALIGNED_LOAD__(";
    printTypeNameUnaligned(Out, OperandType, false);
    if (IsVolatile)
      Out << " volatile";
    Out << ", " << Alignment << ", ";
  }

  writeOperand(Operand);

  if (IsUnaligned) {
    Out << ")";
  }
}

void CWriter::visitLoadInst(LoadInst &I) {
  CurInstr = &I;

  writeMemoryAccess(I.getOperand(0), I.getType(), I.isVolatile(),
                    I.getAlignment());
}

void CWriter::visitStoreInst(StoreInst &I) {
  CurInstr = &I;

  writeMemoryAccess(I.getPointerOperand(), I.getOperand(0)->getType(),
                    I.isVolatile(), I.getAlignment());
  Out << " = ";
  Value *Operand = I.getOperand(0);
  unsigned BitMask = 0;
  if (IntegerType *ITy = dyn_cast<IntegerType>(Operand->getType()))
    if (!ITy->isPowerOf2ByteWidth())
      // We have a bit width that doesn't match an even power-of-2 byte
      // size. Consequently we must & the value with the type's bit mask
      BitMask = ITy->getBitMask();
  if (BitMask)
    Out << "((";
  writeOperand(Operand, BitMask ? ContextNormal : ContextCasted);
  if (BitMask)
    Out << ") & " << BitMask << ")";
}

void CWriter::visitFenceInst(FenceInst &I) {
  Out << "__atomic_thread_fence(";
  switch (I.getOrdering()) {
  case AtomicOrdering::Acquire:
    Out << "__ATOMIC_ACQUIRE";
    break;
  case AtomicOrdering::Release:
    Out << "__ATOMIC_RELEASE";
    break;
  case AtomicOrdering::AcquireRelease:
    Out << "__ATOMIC_ACQ_REL";
    break;
  case AtomicOrdering::SequentiallyConsistent:
    Out << "__ATOMIC_SEQ_CST";
    break;
  case AtomicOrdering::NotAtomic:
  case AtomicOrdering::Unordered:
  case AtomicOrdering::Monotonic:
    Out << "__ATOMIC_RELAXED";
    break;
  default:
    errorWithMessage("Unhandled atomic ordering for fence instruction");
  }
  Out << ");\n";
}

void CWriter::visitGetElementPtrInst(GetElementPtrInst &I) {
  CurInstr = &I;

  printGEPExpression(I.getPointerOperand(), gep_type_begin(I), gep_type_end(I));
}

void CWriter::visitVAArgInst(VAArgInst &I) {
  CurInstr = &I;

  Out << "va_arg(*(va_list*)";
  writeOperand(I.getOperand(0), ContextCasted);
  Out << ", ";
  printTypeName(Out, I.getType());
  Out << ");\n ";
}

// TODO_: Move to header. In general we need to split this enormous file.
class OutModifier {
public:
  size_t last_size;
  std::string &out;
  OutModifier(std::string &out) : out(out) {
    last_size = out.size();
  }
  std::string cut_tail() {
    std::string tail = out.substr(last_size, out.size() - last_size);
    out.resize(last_size);
    return tail;
  }
};

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
#ifndef NDEBUG
      errs() << "Cannot parse '" << index_str << "' as integer\n";
#endif
    errorWithMessage("Cannot access vector element by dynamic index");
  }
  printVectorComponent(Out, index);
  Out << " = ";
  writeOperand(I.getOperand(1), ContextCasted);
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
  #ifndef NDEBUG
        errs() << "Cannot parse '" << index_str << "' as integer\n";
  #endif
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
  printTypeString(Out, VT, false);
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
      printConstant(Zero, ContextCasted);
    } else {
      // If SrcVal belongs [0, n - 1], it extracts value from <v1>
      // If SrcVal belongs [n, 2 * n - 1], it extracts value from <v2>
      // In C++, the value false is converted to zero and the value true is
      // converted to one
      Value *Op = SVI.getOperand((unsigned)SrcVal >= NumInputElts);
      if (isa<Instruction>(Op)) {
        // Do an extractelement of this value from the appropriate input.
        Out << "(";
        writeOperand(Op);
        Out << ").";
        printVectorComponent(Out,
          ((unsigned)SrcVal >= NumInputElts ? SrcVal - NumInputElts : SrcVal)
        );
      } else if (isa<ConstantAggregateZero>(Op) || isa<UndefValue>(Op)) {
        printConstant(Zero, ContextCasted);
      } else {
        printConstant(
            cast<ConstantVector>(Op)->getOperand(SrcVal & (NumElts - 1)),
            ContextNormal);
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
      Out << ".array[" << *i << "]";
    else
      Out << ".field" << *i;
  }
  Out << " = ";
  writeOperand(IVI.getOperand(1), ContextCasted);
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
        Out << ".array[" << *i << "]";
      else
        Out << ".field" << *i;
    }
  }
  Out << ")";
}

LLVM_ATTRIBUTE_NORETURN void CWriter::errorWithMessage(const char *message) {
#ifndef NDEBUG
  errs() << message;
  errs() << " in: ";
  if (CurInstr != nullptr) {
    errs() << *CurInstr << " @ ";
    CurInstr->getDebugLoc().print(errs());
  } else {
    errs() << "<unknown instruction>";
  }
  errs() << "\n";
#endif

  llvm_unreachable(message);
}

} // namespace llvm_cbe
