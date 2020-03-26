__kernel void kernel_main(
    __global const float4 *a,
    __global const float4 *b,
    __global const float4 *c,

    // Unary
    __global float4 *plus,
    __global float4 *minus,
    // Arithmetic
    __global float4 *add,
    __global float4 *sub,
    __global float4 *mul,
    __global float4 *div,
    // Compare
    __global float4 *eq,
    __global float4 *ne,
    __global float4 *gt,
    __global float4 *ge,
    __global float4 *lt,
    __global float4 *le,
    // Select
    __global float4 *select,

    // Arithmetic Assign
    __global float4 *add_assign,
    __global float4 *sub_assign,
    __global float4 *mul_assign,
    __global float4 *div_assign
) {
    int i = get_global_id(0);

    // Unary
    plus[i] = +a[i];
    minus[i] = -a[i];
    
    add[i] = a[i] + b[i];
    sub[i] = a[i] - b[i];
    mul[i] = a[i] * b[i];
    div[i] = a[i] / (fabs(b[i]) + 1); // to avoid division by 0, which is UB

    eq[i] = convert_float4(a[i] == b[i]);
    ne[i] = convert_float4(a[i] != b[i]);
    gt[i] = convert_float4(a[i] > b[i]);
    ge[i] = convert_float4(a[i] >= b[i]);
    lt[i] = convert_float4(a[i] < b[i]);
    le[i] = convert_float4(a[i] <= b[i]);

    select[i] = convert_int4(c[i]) ? a[i] : b[i];
    
    add_assign[i] += a[i];
    sub_assign[i] -= a[i];
    mul_assign[i] *= a[i];
    div_assign[i] /= (fabs(a[i]) + 1); // to avoid division by 0, which is UB
}
