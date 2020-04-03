//===-- CLTargetMachine.cpp - TargetMachine for the OpenCL backend ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the TargetMachine that is used by the OpenCL backend.
//
//===----------------------------------------------------------------------===//

#include "CLTargetMachine.h"
#include "CLBackend.h"
#include "llvm/CodeGen/TargetPassConfig.h"

#if LLVM_VERSION_MAJOR >= 7
#include "llvm/Transforms/Utils.h"
#endif

namespace llvm {

bool CLTargetMachine::addPassesToEmitFile(PassManagerBase &PM,
                                         raw_pwrite_stream &Out,
#if LLVM_VERSION_MAJOR >= 7
                                         raw_pwrite_stream *DwoOut,
#endif
                                         CodeGenFileType FileType,
                                         bool DisableVerify,
                                         MachineModuleInfoWrapperPass *MMI) {

  if (FileType != CGFT_AssemblyFile)
    return true;

  PM.add(new TargetPassConfig(*this, PM));
  PM.add(createGCLoweringPass());

  // Remove exception handling with LowerInvokePass. This would be done with
  // TargetPassConfig if TargetPassConfig supported TargetMachines that aren't
  // LLVMTargetMachines.
  PM.add(createLowerInvokePass());
  PM.add(createUnreachableBlockEliminationPass());

  // Lower atomic operations to libcalls
  PM.add(createAtomicExpandPass());

  PM.add(new llvm_opencl::CWriter(Out));
  return false;
}

const TargetSubtargetInfo *
CLTargetMachine::getSubtargetImpl(const Function &) const {
  return &SubtargetInfo;
}

bool CLTargetSubtargetInfo::enableAtomicExpand() const { return true; }

const TargetLowering *CLTargetSubtargetInfo::getTargetLowering() const {
  return &Lowering;
}

} // namespace llvm
