struct Wrap {
    float3 v;
};

__kernel void kernel_main(
    __global const float *a,
    __global const float *b,
    __global const float *c,
    __global float *d
) {
    int i = get_global_id(0);
    struct Wrap w = { (float3)(a[i], b[i], c[i]) };
    vstore3(w.v, i, d);
}
