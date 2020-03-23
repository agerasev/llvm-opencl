__kernel void kernel_main(__global int *a, __global const int *b) {
    int i = get_global_id(0);

    if (i % 2 == 0) {
        if (i % 3 == 0) {
            a[i] = b[i] + 4;
        } else {
            a[i] = b[i] - 4;
        }
    } else {
        if (i % 4 == 0) {
            a[i] = b[i] * 4;
        } else {
            a[i] = b[i] / 4;
        }
    }
}
