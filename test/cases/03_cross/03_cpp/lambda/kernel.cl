int run(int, int);

__kernel void kernel_main(
    __global const int *a, __global const int *b,
    __global int *c
) {
    const int i = get_global_id(0);
    c[i] = run(a[i], b[i]);
}
