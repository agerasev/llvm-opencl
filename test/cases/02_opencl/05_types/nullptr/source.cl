void write_to(__global int *ptr, int n) {
    if (ptr) {
        *ptr = n;
    }
}

__kernel void kernel_main(__global const int *a, __global int *b) {
    int i = get_global_id(0);
    if (i % 2) {
        write_to(b + i, a[i]);
    } else {
        write_to(0, 0);
    }
}
