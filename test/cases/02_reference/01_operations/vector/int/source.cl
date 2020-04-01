__kernel void kernel_main(
    __global const int4 *a,
    __global const int4 *b,
    __global const int4 *c,

    // Unary
    __global int4 *plus,
    __global int4 *minus,
    // Arithmetic
    __global int4 *add,
    __global int4 *sub,
    __global int4 *mul,
    __global int4 *div,
    __global int4 *mod,
    // Compare
    __global int4 *eq,
    __global int4 *ne,
    __global int4 *gt,
    __global int4 *ge,
    __global int4 *lt,
    __global int4 *le,
    // Bitwise
    __global int4 *bit_not,
    __global int4 *bit_and,
    __global int4 *bit_or,
    __global int4 *bit_xor,
    __global int4 *bit_shl,
    __global int4 *bit_shr,
    // Logical
    __global int4 *not,
    __global int4 *and,
    __global int4 *or,
    // Select
    __global int4 *select,

    // Arithmetic Assign
    __global int4 *add_assign,
    __global int4 *sub_assign,
    __global int4 *mul_assign,
    __global int4 *div_assign,
    __global int4 *mod_assign,
    // Bitwise Assign
    __global int4 *bit_and_assign,
    __global int4 *bit_or_assign,
    __global int4 *bit_xor_assign,
    __global int4 *bit_shl_assign,
    __global int4 *bit_shr_assign,

    // Pre- and postincrement
    __global int4 *pre_inc_src,
    __global int4 *post_inc_src,
    __global int4 *pre_dec_src,
    __global int4 *post_dec_src,
    __global int4 *pre_inc_dst,
    __global int4 *post_inc_dst,
    __global int4 *pre_dec_dst,
    __global int4 *post_dec_dst

) {
    int i = get_global_id(0);

    // Unary
    plus[i] = +a[i];
    minus[i] = -a[i];
    
    add[i] = a[i] + b[i];
    sub[i] = a[i] - b[i];
    mul[i] = a[i] * b[i];
    div[i] = a[i] / (convert_int4(abs(b[i])) + 1); // to avoid division by 0, which is UB
    mod[i] = a[i] % (convert_int4(abs(b[i])) + 1); // to avoid division by 0, which is UB

    eq[i] = a[i] == b[i];
    ne[i] = a[i] != b[i];
    gt[i] = a[i] > b[i];
    ge[i] = a[i] >= b[i];
    lt[i] = a[i] < b[i];
    le[i] = a[i] <= b[i];
    
    bit_not[i] = ~a[i];
    bit_and[i] = a[i] & b[i];
    bit_or[i] = a[i] | b[i];
    bit_xor[i] = a[i] ^ b[i];
    bit_shl[i] = a[i] << c[i];
    bit_shr[i] = a[i] >> c[i];

    not[i] = !a[i];
    and[i] = a[i] && b[i];
    or[i] = a[i] || b[i];

    select[i] = c[i] ? a[i] : b[i];
    
    add_assign[i] += a[i];
    sub_assign[i] -= a[i];
    mul_assign[i] *= a[i];
    div_assign[i] /= (convert_int4(abs(a[i])) + 1); // to avoid division by 0, which is UB
    mod_assign[i] %= (convert_int4(abs(a[i])) + 1); // to avoid division by 0, which is UB
    
    bit_and_assign[i] &= a[i];
    bit_or_assign[i] |= a[i];
    bit_xor_assign[i] ^= a[i];
    bit_shl_assign[i] <<= a[i];
    bit_shr_assign[i] >>= a[i];

    pre_inc_dst[i] = ++pre_inc_src[i];
    post_inc_dst[i] = post_inc_src[i]++;
    pre_dec_dst[i] = --pre_dec_src[i];
    post_dec_dst[i] = post_dec_src[i]--;
}
