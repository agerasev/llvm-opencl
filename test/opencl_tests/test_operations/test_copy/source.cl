__kernel void kernel_main(
    __global const int *a,
    __global const float *b,
    __global int *c,
    __global float *d
) {
    int i = get_global_id(0);
    c[i] = a[i];
    d[i] = b[i];
}
