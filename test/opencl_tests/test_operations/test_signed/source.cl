__kernel void kernel_main(__global int *a, __global uint *b, __global const int *c, __global const uint *d) {
    int i = get_global_id(0);
    a[i] = c[i]*(int)d[i];
    b[i] = (uint)c[i]*d[i];
}
