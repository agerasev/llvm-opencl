__kernel void kernel_main(__global int *dst, __global const int *src) {
    int i = get_global_id(0);
    dst[i] = src[i];
}
