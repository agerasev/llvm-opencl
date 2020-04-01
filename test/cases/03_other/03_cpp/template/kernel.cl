int run_int(int);
float run_float(float);

__kernel void kernel_main(
    __global const int *a, __global const float *b,
    __global int *c, __global float *d
) {
    const int i = get_global_id(0);
    c[i] = run_int(a[i]);
    d[i] = run_float(b[i]);
}
