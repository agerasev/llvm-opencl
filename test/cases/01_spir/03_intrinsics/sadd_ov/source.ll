target datalayout = "e-p:32:32-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024"
target triple = "spir-unknown-unknown"

define dso_local spir_kernel void @kernel_main(
  i32 addrspace(1)* readonly,
  i32 addrspace(1)* readonly,
  i32 addrspace(1)*,
  i8 addrspace(1)*
) {
  %i = tail call spir_func i32 @_Z13get_global_idj(i32 0)
  %ap = getelementptr inbounds i32, i32 addrspace(1)* %0, i32 %i
  %a = load i32, i32 addrspace(1)* %ap, align 4
  %bp = getelementptr inbounds i32, i32 addrspace(1)* %1, i32 %i
  %b = load i32, i32 addrspace(1)* %bp, align 4

  %s = call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %a, i32 %b)
  %c = extractvalue { i32, i1 } %s, 0
  %o1 = extractvalue { i32, i1 } %s, 1
  %o = zext i1 %o1 to i8

  %cp = getelementptr inbounds i32, i32 addrspace(1)* %2, i32 %i
  store i32 %c, i32 addrspace(1)* %cp, align 4
  %op = getelementptr inbounds i8, i8 addrspace(1)* %3, i32 %i
  store i8 %o, i8 addrspace(1)* %op, align 1
  ret void
}

declare dso_local spir_func i32 @_Z13get_global_idj(i32)
declare { i32, i1 } @llvm.sadd.with.overflow.i32(i32, i32)
