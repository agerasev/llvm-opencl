float length2(float4 v) {
    float l = length(v);
    return l*l;
}

float hyperbolic_distance(float4 a, float4 b) {
    float x = 1.0f + length2(a - b)/(2.0f*a.z*b.z);
    return log(x + sqrt(x*x - 1.0f));
}

__kernel void kernel_main(__global const float *a, __global const float *b, __global float *c) {
    int i = get_global_id(0);
    c[i] = hyperbolic_distance(vload4(i, a), vload4(i, b));
}
