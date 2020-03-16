__kernel void kernel_main(__global float *c, __global const float *a, __global const float *b) {
    int i = get_global_id(0);
    float2 av = vload2(i, a);
    float3 bv = vload3(i, b);
    float4 cv = (float4)(av, 0.0f, 0.0f) + (float4)(0.0f, bv.zyx);
    cv.xw += av;
    vstore4(cv, i, c);
}
