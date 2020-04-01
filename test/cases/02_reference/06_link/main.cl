int func(int);

__kernel void kernel_main(__global const int *a, __global int *b) {
    int i = get_global_id(0);
    b[i] = func(a[i]);
}
