__kernel void kernel_main(__global int *a, __global const int *b) {
    int k = get_global_id(0);

    a[k] = 0;
    for (int i = 0; i < k; ++i) {
        for (int j = 0; j < b[i]; ++j) {
            a[k] += 1;
        }
    }
}
