__kernel void kernel_main(__global const int *a, __global int *b) {
    int i = get_global_id(0);
    int arr[5] = { a[i + 0], a[i + 1], a[i + 2], a[i + 3], a[i + 4] };
    b[i] = arr[i%5];
}
