__kernel void kernel_main(__global int *a, __global const int *b) {
    int i = get_global_id(0);

    int c = b[i];
    if (i % 2 == 0) {
        goto label;
    }

    c /= 2;

label:
    a[i] = c;
}
