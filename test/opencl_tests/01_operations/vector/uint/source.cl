__kernel void kernel_main(
    __global const uint4 *a,
    __global const uint4 *b,
    __global const uint4 *c,

    // Unary
    __global uint4 *plus,
    __global uint4 *minus,
    // Arithmetic
    __global uint4 *add,
    __global uint4 *sub,
    __global uint4 *mul,
    __global uint4 *div,
    __global uint4 *mod,
    // Compare
    __global uint4 *eq,
    __global uint4 *ne,
    __global uint4 *gt,
    __global uint4 *ge,
    __global uint4 *lt,
    __global uint4 *le,
    // Bitwise
    __global uint4 *bit_not,
    __global uint4 *bit_and,
    __global uint4 *bit_or,
    __global uint4 *bit_xor,
    __global uint4 *bit_shl,
    __global uint4 *bit_shr,
    // Logical
    __global uint4 *not,
    __global uint4 *and,
    __global uint4 *or,
    // Select
    __global uint4 *select,

    // Arithmetic Assign
    __global uint4 *add_assign,
    __global uint4 *sub_assign,
    __global uint4 *mul_assign,
    __global uint4 *div_assign,
    __global uint4 *mod_assign,
    // Bitwise Assign
    __global uint4 *bit_and_assign,
    __global uint4 *bit_or_assign,
    __global uint4 *bit_xor_assign,
    __global uint4 *bit_shl_assign,
    __global uint4 *bit_shr_assign,

    // Pre- and postincrement
    __global uint4 *pre_inc_src,
    __global uint4 *post_inc_src,
    __global uint4 *pre_dec_src,
    __global uint4 *post_dec_src,
    __global uint4 *pre_inc_dst,
    __global uint4 *post_inc_dst,
    __global uint4 *pre_dec_dst,
    __global uint4 *post_dec_dst

) {
    int i = get_global_id(0);

    // Unary
    plus[i] = +a[i];
    minus[i] = -a[i];
    
    add[i] = a[i] + b[i];
    sub[i] = a[i] - b[i];
    mul[i] = a[i] * b[i];
    div[i] = a[i] / (b[i] + 1); // to avoid division by 0, which is UB
    mod[i] = a[i] % (b[i] + 1); // to avoid division by 0, which is UB
    
    eq[i] = convert_uint4(a[i] == b[i]);
    ne[i] = convert_uint4(a[i] != b[i]);
    gt[i] = convert_uint4(a[i] > b[i]);
    ge[i] = convert_uint4(a[i] >= b[i]);
    lt[i] = convert_uint4(a[i] < b[i]);
    le[i] = convert_uint4(a[i] <= b[i]);
    
    bit_not[i] = ~a[i];
    bit_and[i] = a[i] & b[i];
    bit_or[i] = a[i] | b[i];
    bit_xor[i] = a[i] ^ b[i];
    bit_shl[i] = a[i] << c[i];
    bit_shr[i] = a[i] >> c[i];

    not[i] = convert_uint4(!a[i]);
    and[i] = convert_uint4(a[i] && b[i]);
    or[i] = convert_uint4(a[i] || b[i]);

    select[i] = c[i] ? a[i] : b[i];
    
    add_assign[i] += a[i];
    sub_assign[i] -= a[i];
    mul_assign[i] *= a[i];
    div_assign[i] /= (abs(a[i]) + 1); // to avoid division by 0, which is UB
    mod_assign[i] %= (abs(a[i]) + 1); // to avoid division by 0, which is UB
    
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
