__kernel void kernel_main(__global const int *a, __global const int *b, __global float *c, __global int *d) {
    int i = get_global_id(0);
    c[i] = a[i]*b[i];
    d[i] = a[i] < 0;
}
