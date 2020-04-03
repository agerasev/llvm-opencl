typedef struct {
    int x;
    float y;
} A;

A func(A s) {
    s.x *= 2;
    s.y *= 3.1415f;
    return s;
}

__kernel void kernel_main(__global const int *a, __global const float *b, __global float *c) {
    int i = get_global_id(0);
    A s = func((A) { a[i], b[i] });
    c[i] = (float)s.x + s.y;
}
