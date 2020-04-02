add_subdirectory(TargetInfo)

set(LLVM_LINK_COMPONENTS
  Analysis
  CLBackendInfo
  CodeGen
  Core
  MC
  ScalarOpts
  Support
  Target
  SelectionDAG
  TransformUtils
  )

add_llvm_target(CLBackendCodeGen
  CLBackend.cpp
  CLTargetMachine.cpp
  TopologicalSorter.cpp
  CLBuiltIns.cpp
  StringTools.cpp
  )

set_source_files_properties(CLBackend.cpp PROPERTIES COMPILE_FLAGS -g)
set_source_files_properties(CLBuiltIns.cpp PROPERTIES COMPILE_FLAGS -g)
