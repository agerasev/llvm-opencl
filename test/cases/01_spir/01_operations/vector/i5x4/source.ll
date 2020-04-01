target datalayout = "e-p:32:32-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024"
target triple = "spir-unknown-unknown"

define dso_local spir_kernel void @kernel_main(
  <4 x i8> addrspace(1)* readonly,
  <4 x i8> addrspace(1)* readonly,
  <4 x i8> addrspace(1)* readonly,
  <4 x i8> addrspace(1)* readonly,

  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,

  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,

  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,

  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,
  <4 x i8> addrspace(1)*,

  <4 x i8> addrspace(1)*
) {
  %i = tail call spir_func i32 @_Z13get_global_idj(i32 0)

  %a_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %0, i32 %i
  %a_2 = load <4 x i8>, <4 x i8> addrspace(1)* %a_p, align 4
  %a = trunc <4 x i8> %a_2 to <4 x i5>

  %b_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %1, i32 %i
  %b_2 = load <4 x i8>, <4 x i8> addrspace(1)* %b_p, align 4
  %b = trunc <4 x i8> %b_2 to <4 x i5>

  %c_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %2, i32 %i
  %c_2 = load <4 x i8>, <4 x i8> addrspace(1)* %c_p, align 4
  %c = trunc <4 x i8> %c_2 to <4 x i5>

  %d_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %3, i32 %i
  %d_2 = load <4 x i8>, <4 x i8> addrspace(1)* %d_p, align 4
  %d = trunc <4 x i8> %d_2 to <4 x i5>


  %trunc_x = sext <4 x i5> %a to <4 x i8>
  %trunc_1 = trunc <4 x i8> %trunc_x to <4 x i5>
  %trunc = zext <4 x i5> %trunc_1 to <4 x i8>
  %trunc_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %4, i32 %i
  store <4 x i8> %trunc, <4 x i8> addrspace(1)* %trunc_p, align 4

  %zext = zext <4 x i5> %a to <4 x i8>
  %zext_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %5, i32 %i
  store <4 x i8> %zext, <4 x i8> addrspace(1)* %zext_p, align 4

  %sext = sext <4 x i5> %a to <4 x i8>
  %sext_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %6, i32 %i
  store <4 x i8> %sext, <4 x i8> addrspace(1)* %sext_p, align 4


  %add = add <4 x i5> %a, %b
  %add_x = zext <4 x i5> %add to <4 x i8>
  %add_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %7, i32 %i
  store <4 x i8> %add_x, <4 x i8> addrspace(1)* %add_p, align 4

  %sub = sub <4 x i5> %a, %b
  %sub_x = zext <4 x i5> %sub to <4 x i8>
  %sub_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %8, i32 %i
  store <4 x i8> %sub_x, <4 x i8> addrspace(1)* %sub_p, align 4

  %mul = mul <4 x i5> %a, %b
  %mul_x = zext <4 x i5> %mul to <4 x i8>
  %mul_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %9, i32 %i
  store <4 x i8> %mul_x, <4 x i8> addrspace(1)* %mul_p, align 4

  %udiv = udiv <4 x i5> %a, %c
  %udiv_x = zext <4 x i5> %udiv to <4 x i8>
  %udiv_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %10, i32 %i
  store <4 x i8> %udiv_x, <4 x i8> addrspace(1)* %udiv_p, align 4

  %sdiv = sdiv <4 x i5> %a, %c
  %sdiv_x = zext <4 x i5> %sdiv to <4 x i8>
  %sdiv_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %11, i32 %i
  store <4 x i8> %sdiv_x, <4 x i8> addrspace(1)* %sdiv_p, align 4

  %urem = urem <4 x i5> %a, %c
  %urem_x = zext <4 x i5> %urem to <4 x i8>
  %urem_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %12, i32 %i
  store <4 x i8> %urem_x, <4 x i8> addrspace(1)* %urem_p, align 4

  %srem = srem <4 x i5> %a, %c
  %srem_x = zext <4 x i5> %srem to <4 x i8>
  %srem_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %13, i32 %i
  store <4 x i8> %srem_x, <4 x i8> addrspace(1)* %srem_p, align 4


  %shl = shl <4 x i5> %a, %d
  %shl_x = zext <4 x i5> %shl to <4 x i8>
  %shl_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %14, i32 %i
  store <4 x i8> %shl_x, <4 x i8> addrspace(1)* %shl_p, align 4

  %lshr = lshr <4 x i5> %a, %d
  %lshr_x = zext <4 x i5> %lshr to <4 x i8>
  %lshr_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %15, i32 %i
  store <4 x i8> %lshr_x, <4 x i8> addrspace(1)* %lshr_p, align 4

  %ashr = ashr <4 x i5> %a, %d
  %ashr_x = zext <4 x i5> %ashr to <4 x i8>
  %ashr_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %16, i32 %i
  store <4 x i8> %ashr_x, <4 x i8> addrspace(1)* %ashr_p, align 4

  %and = and <4 x i5> %a, %b
  %and_x = zext <4 x i5> %and to <4 x i8>
  %and_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %17, i32 %i
  store <4 x i8> %and_x, <4 x i8> addrspace(1)* %and_p, align 4

  %or = or <4 x i5> %a, %b
  %or_x = zext <4 x i5> %or to <4 x i8>
  %or_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %18, i32 %i
  store <4 x i8> %or_x, <4 x i8> addrspace(1)* %or_p, align 4

  %xor = xor <4 x i5> %a, %b
  %xor_x = zext <4 x i5> %xor to <4 x i8>
  %xor_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %19, i32 %i
  store <4 x i8> %xor_x, <4 x i8> addrspace(1)* %xor_p, align 4


  %icmp_eq = icmp eq <4 x i5> %a, %b
  %icmp_eq_x = zext <4 x i1> %icmp_eq to <4 x i8>
  %icmp_eq_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %20, i32 %i
  store <4 x i8> %icmp_eq_x, <4 x i8> addrspace(1)* %icmp_eq_p, align 4

  %icmp_ne = icmp ne <4 x i5> %a, %b
  %icmp_ne_x = zext <4 x i1> %icmp_ne to <4 x i8>
  %icmp_ne_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %21, i32 %i
  store <4 x i8> %icmp_ne_x, <4 x i8> addrspace(1)* %icmp_ne_p, align 4

  %icmp_ugt = icmp ugt <4 x i5> %a, %b
  %icmp_ugt_x = zext <4 x i1> %icmp_ugt to <4 x i8>
  %icmp_ugt_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %22, i32 %i
  store <4 x i8> %icmp_ugt_x, <4 x i8> addrspace(1)* %icmp_ugt_p, align 4

  %icmp_uge = icmp uge <4 x i5> %a, %b
  %icmp_uge_x = zext <4 x i1> %icmp_uge to <4 x i8>
  %icmp_uge_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %23, i32 %i
  store <4 x i8> %icmp_uge_x, <4 x i8> addrspace(1)* %icmp_uge_p, align 4

  %icmp_ult = icmp ult <4 x i5> %a, %b
  %icmp_ult_x = zext <4 x i1> %icmp_ult to <4 x i8>
  %icmp_ult_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %24, i32 %i
  store <4 x i8> %icmp_ult_x, <4 x i8> addrspace(1)* %icmp_ult_p, align 4

  %icmp_ule = icmp ule <4 x i5> %a, %b
  %icmp_ule_x = zext <4 x i1> %icmp_ule to <4 x i8>
  %icmp_ule_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %25, i32 %i
  store <4 x i8> %icmp_ule_x, <4 x i8> addrspace(1)* %icmp_ule_p, align 4

  %icmp_sgt = icmp sgt <4 x i5> %a, %b
  %icmp_sgt_x = zext <4 x i1> %icmp_sgt to <4 x i8>
  %icmp_sgt_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %26, i32 %i
  store <4 x i8> %icmp_sgt_x, <4 x i8> addrspace(1)* %icmp_sgt_p, align 4

  %icmp_sge = icmp sge <4 x i5> %a, %b
  %icmp_sge_x = zext <4 x i1> %icmp_sge to <4 x i8>
  %icmp_sge_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %27, i32 %i
  store <4 x i8> %icmp_sge_x, <4 x i8> addrspace(1)* %icmp_sge_p, align 4

  %icmp_slt = icmp slt <4 x i5> %a, %b
  %icmp_slt_x = zext <4 x i1> %icmp_slt to <4 x i8>
  %icmp_slt_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %28, i32 %i
  store <4 x i8> %icmp_slt_x, <4 x i8> addrspace(1)* %icmp_slt_p, align 4

  %icmp_sle = icmp sle <4 x i5> %a, %b
  %icmp_sle_x = zext <4 x i1> %icmp_sle to <4 x i8>
  %icmp_sle_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %29, i32 %i
  store <4 x i8> %icmp_sle_x, <4 x i8> addrspace(1)* %icmp_sle_p, align 4

  %select_c = trunc <4 x i5> %a to <4 x i1>
  %select = select <4 x i1> %select_c, <4 x i5> %b, <4 x i5> %c
  %select_x = zext <4 x i5> %select to <4 x i8>
  %select_p = getelementptr inbounds <4 x i8>, <4 x i8> addrspace(1)* %30, i32 %i
  store <4 x i8> %select_x, <4 x i8> addrspace(1)* %select_p, align 4

  ret void
}

declare dso_local spir_func i32 @_Z13get_global_idj(i32)
