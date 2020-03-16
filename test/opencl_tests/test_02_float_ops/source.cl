__kernel void kernel_main(__global float *c, __global const float *a, __global const float *b) {
    int i = get_global_id(0);
    c[i] = a[i]*(float)((int)(b[i]));
}
