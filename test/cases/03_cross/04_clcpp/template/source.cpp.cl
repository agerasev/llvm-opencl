template <typename T>
T sqr(T x) {
    return x*x;
}

__kernel void kernel_main(
    __global const int *a, __global const float *b,
    __global int *c, __global float *d
) {
    const int i = get_global_id(0);
    c[i] = sqr(a[i]);
    d[i] = sqr(b[i]);
}
