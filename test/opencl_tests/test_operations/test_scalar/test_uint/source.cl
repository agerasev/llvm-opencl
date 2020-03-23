__kernel void kernel_main(
    __global const uint *a,
    __global const uint *b,
    __global const uint *c,

    // Unary
    __global uint *plus,
    __global uint *minus,
    // Arithmetic
    __global uint *add,
    __global uint *sub,
    __global uint *mul,
    __global uint *div,
    __global uint *mod,
    // Compare
    __global uint *eq,
    __global uint *ne,
    __global uint *gt,
    __global uint *ge,
    __global uint *lt,
    __global uint *le,
    // Bitwise
    __global uint *bit_not,
    __global uint *bit_and,
    __global uint *bit_or,
    __global uint *bit_xor,
    __global uint *bit_shl,
    __global uint *bit_shr,
    // Logical
    __global uint *not,
    __global uint *and,
    __global uint *or,
    // Select
    __global uint *select,

    // Arithmetic Assign
    __global uint *add_assign,
    __global uint *sub_assign,
    __global uint *mul_assign,
    __global uint *div_assign,
    __global uint *mod_assign,
    // Bitwise Assign
    __global uint *bit_and_assign,
    __global uint *bit_or_assign,
    __global uint *bit_xor_assign,
    __global uint *bit_shl_assign,
    __global uint *bit_shr_assign,

    // Pre- and postincrement
    __global uint *pre_inc_src,
    __global uint *post_inc_src,
    __global uint *pre_dec_src,
    __global uint *post_dec_src,
    __global uint *pre_inc_dst,
    __global uint *post_inc_dst,
    __global uint *pre_dec_dst,
    __global uint *post_dec_dst

) {
    uint i = get_global_id(0);

    // Unary
    plus[i] = +a[i];
    minus[i] = -a[i];
    
    add[i] = a[i] + b[i];
    sub[i] = a[i] - b[i];
    mul[i] = a[i] * b[i];
    div[i] = a[i] / b[i];
    mod[i] = a[i] % b[i];
    
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
    div_assign[i] /= a[i];
    mod_assign[i] %= a[i];
    
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
