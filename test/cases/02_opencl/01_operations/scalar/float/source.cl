__kernel void kernel_main(
    __global const float *a,
    __global const float *b,
    __global const int *c,

    // Unary
    __global float *plus,
    __global float *minus,
    // Arithmetic
    __global float *add,
    __global float *sub,
    __global float *mul,
    __global float *div,
    // Compare
    __global float *eq,
    __global float *ne,
    __global float *gt,
    __global float *ge,
    __global float *lt,
    __global float *le,
    // Select
    __global float *select,

    // Arithmetic Assign
    __global float *add_assign,
    __global float *sub_assign,
    __global float *mul_assign,
    __global float *div_assign
) {
    int i = get_global_id(0);

    // Unary
    plus[i] = +a[i];
    minus[i] = -a[i];
    
    add[i] = a[i] + b[i];
    sub[i] = a[i] - b[i];
    mul[i] = a[i] * b[i];
    div[i] = a[i] / b[i];
    
    eq[i] = a[i] == b[i];
    ne[i] = a[i] != b[i];
    gt[i] = a[i] > b[i];
    ge[i] = a[i] >= b[i];
    lt[i] = a[i] < b[i];
    le[i] = a[i] <= b[i];

    select[i] = c[i] ? a[i] : b[i];
    
    add_assign[i] += a[i];
    sub_assign[i] -= a[i];
    mul_assign[i] *= a[i];
    div_assign[i] /= a[i];
}
