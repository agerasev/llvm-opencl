__constant float X = 1.0f + 2.0f*M_E_F;

__kernel void kernel_main(
    __global int *c,
    __global float *d
) {
    int i = get_global_id(0);
    c[i] = false ? 0 : 1000;
    d[i] = log(X) + M_PI_F;
}
