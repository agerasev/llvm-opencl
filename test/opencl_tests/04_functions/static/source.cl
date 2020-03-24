static int function(int x) {
    return -x*x;
}

__kernel void kernel_main(__global const int *a, __global int *b) {
    int i = get_global_id(0);
    b[i] = function(a[i]);
}
