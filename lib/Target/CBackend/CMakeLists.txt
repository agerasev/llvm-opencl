add_subdirectory(TargetInfo)

set(LLVM_LINK_COMPONENTS
  Analysis
  CBackendInfo
  CodeGen
  Core
  MC
  ScalarOpts
  Support
  Target
  SelectionDAG
  TransformUtils
  )

add_llvm_target(CBackendCodeGen
  CBackend.cpp
  CTargetMachine.cpp
  TopologicalSorter.cpp
  CLBuiltIns.cpp
  StringTools.cpp
  )

#set_source_files_properties(CBackend.cpp PROPERTIES COMPILE_FLAGS -g)
#set_source_files_properties(CLBuiltIns.cpp PROPERTIES COMPILE_FLAGS -g)
