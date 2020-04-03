typedef struct {
    int x;
    float y;
} A;

A func(int x, float y) {
    return (A) { x, y };
}

__kernel void kernel_main(__global const int *a, __global const float *b, __global float *c) {
    int i = get_global_id(0);
    A s = func(a[i], b[i]);
    c[i] = (float)s.x + s.y;
}
