__kernel void kernel_main(__global int *a, __global const int *b) {
    int i = get_global_id(0);
    int c = 0;
    for (int j = 0; j < i; ++j) {
        c += b[i];
    }
    a[i] = c;
}
