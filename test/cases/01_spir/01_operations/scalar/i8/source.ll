target datalayout = "e-p:32:32-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024"
target triple = "spir-unknown-unknown"

define dso_local spir_kernel void @kernel_main(
  i8 addrspace(1)* readonly,
  i8 addrspace(1)* readonly,
  i8 addrspace(1)* readonly,
  i8 addrspace(1)* readonly,

  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,

  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,

  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,

  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,
  i8 addrspace(1)*,

  i8 addrspace(1)*
) {
  %i = tail call spir_func i32 @_Z13get_global_idj(i32 0)

  %a_p = getelementptr inbounds i8, i8 addrspace(1)* %0, i32 %i
  %a = load i8, i8 addrspace(1)* %a_p, align 1

  %b_p = getelementptr inbounds i8, i8 addrspace(1)* %1, i32 %i
  %b = load i8, i8 addrspace(1)* %b_p, align 1

  %c_p = getelementptr inbounds i8, i8 addrspace(1)* %2, i32 %i
  %c = load i8, i8 addrspace(1)* %c_p, align 1

  %d_p = getelementptr inbounds i8, i8 addrspace(1)* %3, i32 %i
  %d = load i8, i8 addrspace(1)* %d_p, align 1


  %trunc_p = getelementptr inbounds i8, i8 addrspace(1)* %4, i32 %i
  store i8 %a, i8 addrspace(1)* %trunc_p, align 1

  %zext_p = getelementptr inbounds i8, i8 addrspace(1)* %5, i32 %i
  store i8 %a, i8 addrspace(1)* %zext_p, align 1

  %sext_p = getelementptr inbounds i8, i8 addrspace(1)* %6, i32 %i
  store i8 %a, i8 addrspace(1)* %sext_p, align 1


  %add = add i8 %a, %b
  %add_p = getelementptr inbounds i8, i8 addrspace(1)* %7, i32 %i
  store i8 %add, i8 addrspace(1)* %add_p, align 1

  %sub = sub i8 %a, %b
  %sub_p = getelementptr inbounds i8, i8 addrspace(1)* %8, i32 %i
  store i8 %sub, i8 addrspace(1)* %sub_p, align 1

  %mul = mul i8 %a, %b
  %mul_p = getelementptr inbounds i8, i8 addrspace(1)* %9, i32 %i
  store i8 %mul, i8 addrspace(1)* %mul_p, align 1

  %udiv = udiv i8 %a, %c
  %udiv_p = getelementptr inbounds i8, i8 addrspace(1)* %10, i32 %i
  store i8 %udiv, i8 addrspace(1)* %udiv_p, align 1

  %sdiv = sdiv i8 %a, %c
  %sdiv_p = getelementptr inbounds i8, i8 addrspace(1)* %11, i32 %i
  store i8 %sdiv, i8 addrspace(1)* %sdiv_p, align 1

  %urem = urem i8 %a, %c
  %urem_p = getelementptr inbounds i8, i8 addrspace(1)* %12, i32 %i
  store i8 %urem, i8 addrspace(1)* %urem_p, align 1

  %srem = srem i8 %a, %c
  %srem_p = getelementptr inbounds i8, i8 addrspace(1)* %13, i32 %i
  store i8 %srem, i8 addrspace(1)* %srem_p, align 1


  %shl = shl i8 %a, %d
  %shl_p = getelementptr inbounds i8, i8 addrspace(1)* %14, i32 %i
  store i8 %shl, i8 addrspace(1)* %shl_p, align 1

  %lshr = lshr i8 %a, %d
  %lshr_p = getelementptr inbounds i8, i8 addrspace(1)* %15, i32 %i
  store i8 %lshr, i8 addrspace(1)* %lshr_p, align 1

  %ashr = ashr i8 %a, %d
  %ashr_p = getelementptr inbounds i8, i8 addrspace(1)* %16, i32 %i
  store i8 %ashr, i8 addrspace(1)* %ashr_p, align 1

  %and = and i8 %a, %b
  %and_p = getelementptr inbounds i8, i8 addrspace(1)* %17, i32 %i
  store i8 %and, i8 addrspace(1)* %and_p, align 1

  %or = or i8 %a, %b
  %or_p = getelementptr inbounds i8, i8 addrspace(1)* %18, i32 %i
  store i8 %or, i8 addrspace(1)* %or_p, align 1

  %xor = xor i8 %a, %b
  %xor_p = getelementptr inbounds i8, i8 addrspace(1)* %19, i32 %i
  store i8 %xor, i8 addrspace(1)* %xor_p, align 1


  %icmp_eq = icmp eq i8 %a, %b
  %icmp_eq_x = zext i1 %icmp_eq to i8
  %icmp_eq_p = getelementptr inbounds i8, i8 addrspace(1)* %20, i32 %i
  store i8 %icmp_eq_x, i8 addrspace(1)* %icmp_eq_p, align 1

  %icmp_ne = icmp ne i8 %a, %b
  %icmp_ne_x = zext i1 %icmp_ne to i8
  %icmp_ne_p = getelementptr inbounds i8, i8 addrspace(1)* %21, i32 %i
  store i8 %icmp_ne_x, i8 addrspace(1)* %icmp_ne_p, align 1

  %icmp_ugt = icmp ugt i8 %a, %b
  %icmp_ugt_x = zext i1 %icmp_ugt to i8
  %icmp_ugt_p = getelementptr inbounds i8, i8 addrspace(1)* %22, i32 %i
  store i8 %icmp_ugt_x, i8 addrspace(1)* %icmp_ugt_p, align 1

  %icmp_uge = icmp uge i8 %a, %b
  %icmp_uge_x = zext i1 %icmp_uge to i8
  %icmp_uge_p = getelementptr inbounds i8, i8 addrspace(1)* %23, i32 %i
  store i8 %icmp_uge_x, i8 addrspace(1)* %icmp_uge_p, align 1

  %icmp_ult = icmp ult i8 %a, %b
  %icmp_ult_x = zext i1 %icmp_ult to i8
  %icmp_ult_p = getelementptr inbounds i8, i8 addrspace(1)* %24, i32 %i
  store i8 %icmp_ult_x, i8 addrspace(1)* %icmp_ult_p, align 1

  %icmp_ule = icmp ule i8 %a, %b
  %icmp_ule_x = zext i1 %icmp_ule to i8
  %icmp_ule_p = getelementptr inbounds i8, i8 addrspace(1)* %25, i32 %i
  store i8 %icmp_ule_x, i8 addrspace(1)* %icmp_ule_p, align 1

  %icmp_sgt = icmp sgt i8 %a, %b
  %icmp_sgt_x = zext i1 %icmp_sgt to i8
  %icmp_sgt_p = getelementptr inbounds i8, i8 addrspace(1)* %26, i32 %i
  store i8 %icmp_sgt_x, i8 addrspace(1)* %icmp_sgt_p, align 1

  %icmp_sge = icmp sge i8 %a, %b
  %icmp_sge_x = zext i1 %icmp_sge to i8
  %icmp_sge_p = getelementptr inbounds i8, i8 addrspace(1)* %27, i32 %i
  store i8 %icmp_sge_x, i8 addrspace(1)* %icmp_sge_p, align 1

  %icmp_slt = icmp slt i8 %a, %b
  %icmp_slt_x = zext i1 %icmp_slt to i8
  %icmp_slt_p = getelementptr inbounds i8, i8 addrspace(1)* %28, i32 %i
  store i8 %icmp_slt_x, i8 addrspace(1)* %icmp_slt_p, align 1

  %icmp_sle = icmp sle i8 %a, %b
  %icmp_sle_x = zext i1 %icmp_sle to i8
  %icmp_sle_p = getelementptr inbounds i8, i8 addrspace(1)* %29, i32 %i
  store i8 %icmp_sle_x, i8 addrspace(1)* %icmp_sle_p, align 1

  %select_c = trunc i8 %a to i1
  %select = select i1 %select_c, i8 %b, i8 %c
  %select_p = getelementptr inbounds i8, i8 addrspace(1)* %30, i32 %i
  store i8 %select, i8 addrspace(1)* %select_p, align 1

  ret void
}

declare dso_local spir_func i32 @_Z13get_global_idj(i32)
