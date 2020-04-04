typedef struct __attribute__((aligned(16))) {
    char a __attribute__((aligned(4)));
    short b __attribute__((aligned(4)));
    int c __attribute__((aligned(8)));
    long d __attribute__((aligned(8)));
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
