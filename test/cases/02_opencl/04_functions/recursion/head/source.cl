static int triangle(int x) {
    if (x > 0) {
        return x + triangle(x - 1);
    }
    return 0;
}

__kernel void kernel_main(__global const int *a, __global int *b) {
    int i = get_global_id(0);
    b[i] = triangle(a[i]);
}
