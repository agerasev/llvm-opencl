__kernel void kernel_main(
    __global const bool *a,
    __global const bool *b,
    __global const bool *c,

    // Unary
    __global bool *plus,
    __global bool *minus,
    // Arithmetic
    __global bool *add,
    __global bool *sub,
    __global bool *mul,
    __global bool *div,
    __global bool *mod,
    // Compare
    __global bool *eq,
    __global bool *ne,
    __global bool *gt,
    __global bool *ge,
    __global bool *lt,
    __global bool *le,
    // Bitwise
    __global bool *bit_not,
    __global bool *bit_and,
    __global bool *bit_or,
    __global bool *bit_xor,
    __global bool *bit_shl,
    __global bool *bit_shr,
    // Logical
    __global bool *not,
    __global bool *and,
    __global bool *or,
    // Select
    __global bool *select,

    // Arithmetic Assign
    __global bool *add_assign,
    __global bool *sub_assign,
    __global bool *mul_assign,
    __global bool *div_assign,
    __global bool *mod_assign,
    // Bitwise Assign
    __global bool *bit_and_assign,
    __global bool *bit_or_assign,
    __global bool *bit_xor_assign,
    __global bool *bit_shl_assign,
    __global bool *bit_shr_assign,

    // Pre- and postincrement
    __global bool *pre_inc_src,
    __global bool *post_inc_src,
    __global bool *pre_dec_src,
    __global bool *post_dec_src,
    __global bool *pre_inc_dst,
    __global bool *post_inc_dst,
    __global bool *pre_dec_dst,
    __global bool *post_dec_dst

) {
    uint i = get_global_id(0);

    // Unary
    plus[i] = +a[i];
    minus[i] = -a[i];
    
    add[i] = a[i] + b[i];
    sub[i] = a[i] - b[i];
    mul[i] = a[i] * b[i];
    div[i] = a[i] / true; // UB otherwise
    mod[i] = a[i] % true; // UB otherwise
    
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
    div_assign[i] /= true; // UB otherwise
    mod_assign[i] %= true; // UB otherwise
    
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
