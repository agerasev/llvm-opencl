target datalayout = "e-p:32:32-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024"
target triple = "spir-unknown-unknown"

define dso_local spir_kernel void @kernel_main(
  i16 addrspace(1)* readonly,
  i16 addrspace(1)* readonly,
  i16 addrspace(1)* readonly,
  i16 addrspace(1)* readonly,

  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,

  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,

  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,

  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,
  i16 addrspace(1)*,

  i16 addrspace(1)*
) {
  %i = tail call spir_func i32 @_Z13get_global_idj(i32 0)

  %a_p = getelementptr inbounds i16, i16 addrspace(1)* %0, i32 %i
  %a_2 = load i16, i16 addrspace(1)* %a_p, align 2
  %a = trunc i16 %a_2 to i9

  %b_p = getelementptr inbounds i16, i16 addrspace(1)* %1, i32 %i
  %b_2 = load i16, i16 addrspace(1)* %b_p, align 2
  %b = trunc i16 %b_2 to i9

  %c_p = getelementptr inbounds i16, i16 addrspace(1)* %2, i32 %i
  %c_2 = load i16, i16 addrspace(1)* %c_p, align 2
  %c = trunc i16 %c_2 to i9

  %d_p = getelementptr inbounds i16, i16 addrspace(1)* %3, i32 %i
  %d_2 = load i16, i16 addrspace(1)* %d_p, align 2
  %d = trunc i16 %d_2 to i9


  %trunc_x = sext i9 %a to i16
  %trunc_1 = trunc i16 %trunc_x to i9
  %trunc = zext i9 %trunc_1 to i16
  %trunc_p = getelementptr inbounds i16, i16 addrspace(1)* %4, i32 %i
  store i16 %trunc, i16 addrspace(1)* %trunc_p, align 2

  %zext = zext i9 %a to i16
  %zext_p = getelementptr inbounds i16, i16 addrspace(1)* %5, i32 %i
  store i16 %zext, i16 addrspace(1)* %zext_p, align 2

  %sext = sext i9 %a to i16
  %sext_p = getelementptr inbounds i16, i16 addrspace(1)* %6, i32 %i
  store i16 %sext, i16 addrspace(1)* %sext_p, align 2


  %add = add i9 %a, %b
  %add_x = zext i9 %add to i16
  %add_p = getelementptr inbounds i16, i16 addrspace(1)* %7, i32 %i
  store i16 %add_x, i16 addrspace(1)* %add_p, align 2

  %sub = sub i9 %a, %b
  %sub_x = zext i9 %sub to i16
  %sub_p = getelementptr inbounds i16, i16 addrspace(1)* %8, i32 %i
  store i16 %sub_x, i16 addrspace(1)* %sub_p, align 2

  %mul = mul i9 %a, %b
  %mul_x = zext i9 %mul to i16
  %mul_p = getelementptr inbounds i16, i16 addrspace(1)* %9, i32 %i
  store i16 %mul_x, i16 addrspace(1)* %mul_p, align 2

  %udiv = udiv i9 %a, %c
  %udiv_x = zext i9 %udiv to i16
  %udiv_p = getelementptr inbounds i16, i16 addrspace(1)* %10, i32 %i
  store i16 %udiv_x, i16 addrspace(1)* %udiv_p, align 2

  %sdiv = sdiv i9 %a, %c
  %sdiv_x = zext i9 %sdiv to i16
  %sdiv_p = getelementptr inbounds i16, i16 addrspace(1)* %11, i32 %i
  store i16 %sdiv_x, i16 addrspace(1)* %sdiv_p, align 2

  %urem = urem i9 %a, %c
  %urem_x = zext i9 %urem to i16
  %urem_p = getelementptr inbounds i16, i16 addrspace(1)* %12, i32 %i
  store i16 %urem_x, i16 addrspace(1)* %urem_p, align 2

  %srem = srem i9 %a, %c
  %srem_x = zext i9 %srem to i16
  %srem_p = getelementptr inbounds i16, i16 addrspace(1)* %13, i32 %i
  store i16 %srem_x, i16 addrspace(1)* %srem_p, align 2


  %shl = shl i9 %a, %d
  %shl_x = zext i9 %shl to i16
  %shl_p = getelementptr inbounds i16, i16 addrspace(1)* %14, i32 %i
  store i16 %shl_x, i16 addrspace(1)* %shl_p, align 2

  %lshr = lshr i9 %a, %d
  %lshr_x = zext i9 %lshr to i16
  %lshr_p = getelementptr inbounds i16, i16 addrspace(1)* %15, i32 %i
  store i16 %lshr_x, i16 addrspace(1)* %lshr_p, align 2

  %ashr = ashr i9 %a, %d
  %ashr_x = zext i9 %ashr to i16
  %ashr_p = getelementptr inbounds i16, i16 addrspace(1)* %16, i32 %i
  store i16 %ashr_x, i16 addrspace(1)* %ashr_p, align 2

  %and = and i9 %a, %b
  %and_x = zext i9 %and to i16
  %and_p = getelementptr inbounds i16, i16 addrspace(1)* %17, i32 %i
  store i16 %and_x, i16 addrspace(1)* %and_p, align 2

  %or = or i9 %a, %b
  %or_x = zext i9 %or to i16
  %or_p = getelementptr inbounds i16, i16 addrspace(1)* %18, i32 %i
  store i16 %or_x, i16 addrspace(1)* %or_p, align 2

  %xor = xor i9 %a, %b
  %xor_x = zext i9 %xor to i16
  %xor_p = getelementptr inbounds i16, i16 addrspace(1)* %19, i32 %i
  store i16 %xor_x, i16 addrspace(1)* %xor_p, align 2


  %icmp_eq = icmp eq i9 %a, %b
  %icmp_eq_x = zext i1 %icmp_eq to i16
  %icmp_eq_p = getelementptr inbounds i16, i16 addrspace(1)* %20, i32 %i
  store i16 %icmp_eq_x, i16 addrspace(1)* %icmp_eq_p, align 2

  %icmp_ne = icmp ne i9 %a, %b
  %icmp_ne_x = zext i1 %icmp_ne to i16
  %icmp_ne_p = getelementptr inbounds i16, i16 addrspace(1)* %21, i32 %i
  store i16 %icmp_ne_x, i16 addrspace(1)* %icmp_ne_p, align 2

  %icmp_ugt = icmp ugt i9 %a, %b
  %icmp_ugt_x = zext i1 %icmp_ugt to i16
  %icmp_ugt_p = getelementptr inbounds i16, i16 addrspace(1)* %22, i32 %i
  store i16 %icmp_ugt_x, i16 addrspace(1)* %icmp_ugt_p, align 2

  %icmp_uge = icmp uge i9 %a, %b
  %icmp_uge_x = zext i1 %icmp_uge to i16
  %icmp_uge_p = getelementptr inbounds i16, i16 addrspace(1)* %23, i32 %i
  store i16 %icmp_uge_x, i16 addrspace(1)* %icmp_uge_p, align 2

  %icmp_ult = icmp ult i9 %a, %b
  %icmp_ult_x = zext i1 %icmp_ult to i16
  %icmp_ult_p = getelementptr inbounds i16, i16 addrspace(1)* %24, i32 %i
  store i16 %icmp_ult_x, i16 addrspace(1)* %icmp_ult_p, align 2

  %icmp_ule = icmp ule i9 %a, %b
  %icmp_ule_x = zext i1 %icmp_ule to i16
  %icmp_ule_p = getelementptr inbounds i16, i16 addrspace(1)* %25, i32 %i
  store i16 %icmp_ule_x, i16 addrspace(1)* %icmp_ule_p, align 2

  %icmp_sgt = icmp sgt i9 %a, %b
  %icmp_sgt_x = zext i1 %icmp_sgt to i16
  %icmp_sgt_p = getelementptr inbounds i16, i16 addrspace(1)* %26, i32 %i
  store i16 %icmp_sgt_x, i16 addrspace(1)* %icmp_sgt_p, align 2

  %icmp_sge = icmp sge i9 %a, %b
  %icmp_sge_x = zext i1 %icmp_sge to i16
  %icmp_sge_p = getelementptr inbounds i16, i16 addrspace(1)* %27, i32 %i
  store i16 %icmp_sge_x, i16 addrspace(1)* %icmp_sge_p, align 2

  %icmp_slt = icmp slt i9 %a, %b
  %icmp_slt_x = zext i1 %icmp_slt to i16
  %icmp_slt_p = getelementptr inbounds i16, i16 addrspace(1)* %28, i32 %i
  store i16 %icmp_slt_x, i16 addrspace(1)* %icmp_slt_p, align 2

  %icmp_sle = icmp sle i9 %a, %b
  %icmp_sle_x = zext i1 %icmp_sle to i16
  %icmp_sle_p = getelementptr inbounds i16, i16 addrspace(1)* %29, i32 %i
  store i16 %icmp_sle_x, i16 addrspace(1)* %icmp_sle_p, align 2

  %select_c = trunc i9 %a to i1
  %select = select i1 %select_c, i9 %b, i9 %c
  %select_x = zext i9 %select to i16
  %select_p = getelementptr inbounds i16, i16 addrspace(1)* %30, i32 %i
  store i16 %select_x, i16 addrspace(1)* %select_p, align 2

  ret void
}

declare dso_local spir_func i32 @_Z13get_global_idj(i32)
