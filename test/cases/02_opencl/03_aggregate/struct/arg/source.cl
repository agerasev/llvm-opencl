typedef struct {
    int x;
    float y;
} A;

float func(A s) {
    return (float)s.x + s.y;
}

__kernel void kernel_main(__global const int *a, __global const float *b, __global float *c) {
    int i = get_global_id(0);
    A s = { a[i], b[i] };
    c[i] = func(s);
}
