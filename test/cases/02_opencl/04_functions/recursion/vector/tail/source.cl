static int4 triangle(int4 x) {
    if (all(x <= 0)) {
        return (int4)(0);
    }
    return x + triangle(max(x - 1, 0));
}

__kernel void kernel_main(__global const int *a, __global int *b) {
    int i = get_global_id(0);
    vstore4(triangle(vload4(i, a)), i, b);
}
