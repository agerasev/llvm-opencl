# Support plugins.
set(LLVM_NO_DEAD_STRIP 1)

if (NOT USE_SYSTEM_LLVM)
  set(LLVM_LINK_COMPONENTS
    ${LLVM_TARGETS_TO_BUILD}
    Analysis
    AsmParser
    AsmPrinter
    BitReader
    CLBackendCodeGen
    CLBackendInfo
    CodeGen
    Core
    IRReader
    MC
    ScalarOpts
    SelectionDAG
    Support
    Target
    )


  add_llvm_tool(llvm-opencl
    llvm-opencl.cpp
    )
  export_executable_symbols(llvm-opencl)
endif()

if(USE_SYSTEM_LLVM)
  add_executable(llvm-opencl
    llvm-opencl.cpp
    )
  llvm_map_components_to_libnames(llvm_libs
    CLBackendCodeGen
    CLBackendInfo
    )
  target_link_libraries(llvm-opencl LLVM ${llvm_libs})
endif()
