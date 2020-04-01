__kernel void kernel_main(
    __global const int *a,
    __global const int *b,
    __global const int *c,

    // Unary
    __global int *plus,
    __global int *minus,
    // Arithmetic
    __global int *add,
    __global int *sub,
    __global int *mul,
    __global int *div,
    __global int *mod,
    // Compare
    __global int *eq,
    __global int *ne,
    __global int *gt,
    __global int *ge,
    __global int *lt,
    __global int *le,
    // Bitwise
    __global int *bit_not,
    __global int *bit_and,
    __global int *bit_or,
    __global int *bit_xor,
    __global int *bit_shl,
    __global int *bit_shr,
    // Logical
    __global int *not,
    __global int *and,
    __global int *or,
    // Select
    __global int *select,

    // Arithmetic Assign
    __global int *add_assign,
    __global int *sub_assign,
    __global int *mul_assign,
    __global int *div_assign,
    __global int *mod_assign,
    // Bitwise Assign
    __global int *bit_and_assign,
    __global int *bit_or_assign,
    __global int *bit_xor_assign,
    __global int *bit_shl_assign,
    __global int *bit_shr_assign,

    // Pre- and postincrement
    __global int *pre_inc_src,
    __global int *post_inc_src,
    __global int *pre_dec_src,
    __global int *post_dec_src,
    __global int *pre_inc_dst,
    __global int *post_inc_dst,
    __global int *pre_dec_dst,
    __global int *post_dec_dst

) {
    int i = get_global_id(0);

    // Unary
    plus[i] = +a[i];
    minus[i] = -a[i];
    
    add[i] = a[i] + b[i];
    sub[i] = a[i] - b[i];
    mul[i] = a[i] * b[i];
    div[i] = a[i] / (abs(b[i]) + 1); // to avoid division by 0, which is UB
    mod[i] = a[i] % (abs(b[i]) + 1); // to avoid division by 0, which is UB
    
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
