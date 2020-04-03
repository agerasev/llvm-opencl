typedef struct __attribute__((packed)) {
    char a;
    short b;
    int c;
    long d;
} S;

__kernel void kernel_main(
    __global const char *a,
    __global const short *b,
    __global const int *c,
    __global const long *d,
    __global S *e
) {
    int i = get_global_id(0);
    S s = {a[i], b[i], c[i], d[i]};
    e[i] = s;
}
