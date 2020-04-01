target datalayout = "e-p:32:32-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024"
target triple = "spir-unknown-unknown"

define dso_local spir_kernel void @kernel_main(i32 addrspace(1)* readonly, i32 addrspace(1)*) {
  %3 = tail call spir_func i32 @_Z13get_global_idj(i32 0)
  %4 = getelementptr inbounds i32, i32 addrspace(1)* %0, i32 %3
  %5 = load i32, i32 addrspace(1)* %4, align 4
  %6 = getelementptr inbounds i32, i32 addrspace(1)* %1, i32 %3
  store i32 %5, i32 addrspace(1)* %6, align 4
  ret void
}

declare dso_local spir_func i32 @_Z13get_global_idj(i32)
