//===-- CLBackendTargetInfo.cpp - CLBackend Target Implementation -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../CLTargetMachine.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

Target llvm::TheCLBackendTarget;

extern "C" void LLVMInitializeCLBackendTargetInfo() {
  RegisterTarget<> X(TheCLBackendTarget, "cl", "OpenCL backend", "CL");
}

extern "C" void LLVMInitializeCLBackendTargetMC() {}
