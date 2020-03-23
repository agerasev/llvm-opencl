__kernel void kernel_main(__global int *a, __global const int *b) {
    int i = get_global_id(0);

    switch (i % 4) {
    case 0:
        a[i] = b[i] + 4;
        break;
    case 1:
        a[i] = b[i] - 4;
        break;
    case 2:
        a[i] = b[i] * 4;
        break;
    case 3:
        a[i] = b[i] / 4;
        break;
    }
}
