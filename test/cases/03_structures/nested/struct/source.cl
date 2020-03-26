typedef struct {
    int x;
    float3 y;
} A;

typedef struct {
    A a;
    bool z;
} B;

int get_num(const B *b) {
    return (int)(length(b->a.y)*b->a.x) - b->z;
}

__kernel void kernel_main(__global int *a, __global const int *b, __global const float *c) {
    int i = get_global_id(0);
    B cs = { { 10, (float3)(0.0f, 1.0f, 2.0f) }, true };
    B s = { { b[i], vload3(i, c) }, (b[i] % 2) == 0 };
    a[i] = get_num(&cs) + get_num(&s) + sizeof(B);
}
