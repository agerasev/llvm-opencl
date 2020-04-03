__kernel void kernel_main(
    __global const int *a,
    __global const int *b,
    __global const int *c,
    __global int *d,
    __global int *e
) {
    int i = get_global_id(0);
    vstore4(vload4(i, c) ? vload4(i, a) : vload4(i, b), i, d);
    vstore4((i % 2) ? vload4(i, a) : (vload4(i, b)), i, d);
}
