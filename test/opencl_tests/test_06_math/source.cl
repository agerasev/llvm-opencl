__kernel void kernel_main(__global float *a, __global int *b, __global const float *c) {
    int i = get_global_id(0);
    int4 exp;
    vstore4(frexp(vload4(i, c), &exp), i, a);
    //exp += vload4(i, b);
    vstore4(exp, i, b);
}
