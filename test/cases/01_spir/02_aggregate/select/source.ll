target datalayout = "e-p:32:32-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024"
target triple = "spir-unknown-unknown"

define dso_local spir_kernel void @kernel_main(
  <4 x i32> addrspace(1)* readonly,
  <4 x i32> addrspace(1)* readonly,
  i32 addrspace(1)* readonly,
  <4 x i32> addrspace(1)* readonly,
  <4 x i32> addrspace(1)*,
  <4 x i32> addrspace(1)*
) {
  %i = tail call spir_func i32 @_Z13get_global_idj(i32 0)
  %avp = getelementptr inbounds <4 x i32>, <4 x i32> addrspace(1)* %0, i32 %i
  %av = load <4 x i32>, <4 x i32> addrspace(1)* %avp, align 16
  %bvp = getelementptr inbounds <4 x i32>, <4 x i32> addrspace(1)* %1, i32 %i
  %bv = load <4 x i32>, <4 x i32> addrspace(1)* %bvp, align 16
  %csp = getelementptr inbounds i32, i32 addrspace(1)* %2, i32 %i
  %cs = load i32, i32 addrspace(1)* %csp, align 4
  %cvp = getelementptr inbounds <4 x i32>, <4 x i32> addrspace(1)* %3, i32 %i
  %cv = load <4 x i32>, <4 x i32> addrspace(1)* %cvp, align 16

  %cs1 = trunc i32 %cs to i1
  %ss = select i1 %cs1, <4 x i32> %av, <4 x i32> %bv
  %cv1 = trunc <4 x i32> %cv to <4 x i1>
  %sv = select <4 x i1> %cv1, <4 x i32> %av, <4 x i32> %bv

  %ssp = getelementptr inbounds <4 x i32>, <4 x i32> addrspace(1)* %4, i32 %i
  store <4 x i32> %ss, <4 x i32> addrspace(1)* %ssp, align 16
  %svp = getelementptr inbounds <4 x i32>, <4 x i32> addrspace(1)* %5, i32 %i
  store <4 x i32> %sv, <4 x i32> addrspace(1)* %svp, align 16

  ret void
}

declare dso_local spir_func i32 @_Z13get_global_idj(i32)
