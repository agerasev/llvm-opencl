__kernel void kernel_main(__global int *a, __global const uint *b) {
    int i = get_global_id(0);
    vstore4(convert_int4(vload4(i, b)), i, a);
}
