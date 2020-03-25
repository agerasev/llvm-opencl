//===-- CLTargetMachine.h - TargetMachine for the OpenCL backend ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the TargetMachine that is used by the OpenCL backend.
//
//===----------------------------------------------------------------------===//

#ifndef CLTARGETMACHINE_H
#define CLTARGETMACHINE_H

#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

class CLTargetLowering : public TargetLowering {
public:
  explicit CLTargetLowering(const TargetMachine &TM) : TargetLowering(TM) {
    setMaxAtomicSizeInBitsSupported(0);
  }
};

class CLTargetSubtargetInfo : public TargetSubtargetInfo {
public:
  CLTargetSubtargetInfo(const TargetMachine &TM, const Triple &TT, StringRef CPU,
                       StringRef FS)
      : TargetSubtargetInfo(TT, CPU, FS, ArrayRef<SubtargetFeatureKV>(),
                            ArrayRef<SubtargetFeatureKV>(), nullptr, nullptr,
                            nullptr, nullptr, nullptr, nullptr, nullptr),
        Lowering(TM) {}
  bool enableAtomicExpand() const override;
  const TargetLowering *getTargetLowering() const override;
  const CLTargetLowering Lowering;
};

class CLTargetMachine : public LLVMTargetMachine {
public:
  CLTargetMachine(const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
                 const TargetOptions &Options, Optional<Reloc::Model> RM,
                 Optional<CodeModel::Model> CM, CodeGenOpt::Level OL,
                 bool /*JIT*/)
      : LLVMTargetMachine(T, "", TT, CPU, FS, Options,
                          RM.hasValue() ? RM.getValue() : Reloc::Static,
                          CM.hasValue() ? CM.getValue() : CodeModel::Small, OL),
        SubtargetInfo(*this, TT, CPU, FS) {}

  /// Add passes to the specified pass manager to get the specified file
  /// emitted.  Typically this will involve several steps of code generation.
  bool addPassesToEmitFile(PassManagerBase &PM, raw_pwrite_stream &Out,
#if LLVM_VERSION_MAJOR >= 7
                           raw_pwrite_stream *DwoOut,
#endif
                           CodeGenFileType FileType, bool DisableVerify = true,
                           MachineModuleInfo *MMI = nullptr) override;

  // TargetMachine interface
  const TargetSubtargetInfo *getSubtargetImpl(const Function &) const override;
  const CLTargetSubtargetInfo SubtargetInfo;
};

extern Target TheCLBackendTarget;

} // namespace llvm

#endif
