__kernel void kernel_main(__global const int *a, __global int *b) {
    int i = get_global_id(0);
    int k = -i;
    const int *p;
    if (i % 2) {
        p = &k;
    } else {
        p = /*(const int*)(ulong)*/(a + i);
    }
    b[i] = *p;
}
