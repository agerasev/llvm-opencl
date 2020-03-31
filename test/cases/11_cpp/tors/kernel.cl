void run(int i, int *a, int *b);

__kernel void kernel_main(__global int *a, __global int *b) {
    const int i = get_global_id(0);
    int ctor = 0, dtor = 0;
    run(i, &ctor, &dtor);
    a[i] = ctor;
    b[i] = dtor;
}
