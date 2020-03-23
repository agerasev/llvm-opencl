__kernel void kernel_main(__global int *a, __global const int *b) {
    int i = get_global_id(0);

    if (i % 2 == 0) {
        a[i] = b[i];
    } else {
        a[i] = -b[i];
    }

    if (i % 3) {
        a[i] *= 2;
    }

    if (i % 4) {} else {
        a[i] /= 2;
    }
}
