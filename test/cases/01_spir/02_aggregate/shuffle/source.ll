target datalayout = "e-p:32:32-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024"
target triple = "spir-unknown-unknown"

define dso_local spir_kernel void @kernel_main(
  <4 x float> addrspace(1)* readonly,
  <2 x float> addrspace(1)*
) {
  %i = tail call spir_func i32 @_Z13get_global_idj(i32 0)
  %ivp = getelementptr inbounds <4 x float>, <4 x float> addrspace(1)* %0, i32 %i
  %iv = load <4 x float>, <4 x float> addrspace(1)* %ivp, align 16

  %ov = shufflevector <4 x float> %iv, <4 x float> undef, <2 x i32> <i32 0, i32 2>

  %ovp = getelementptr inbounds <2 x float>, <2 x float> addrspace(1)* %1, i32 %i
  store <2 x float> %ov, <2 x float> addrspace(1)* %ovp, align 8
  ret void
}

declare dso_local spir_func i32 @_Z13get_global_idj(i32)
