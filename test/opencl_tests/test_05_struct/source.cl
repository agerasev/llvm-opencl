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
    B s = {
        {
            b[i],
            vload3(i, c),
        },
        true
    };
    a[i] = get_num(&s);
}
