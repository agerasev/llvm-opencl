__kernel void kernel_main(__global int *a, __global const int *b) {
    int i = get_global_id(0);

    switch(i % 2) {
    case 0:
        switch (i % 3) {
        case 0:
            a[i] = b[i] + 3;
            break;
        case 1:
            a[i] = b[i] - 3;
            break;
        case 2:
            a[i] = b[i] * 3;
            break;
        }
        break;
    case 1:
        a[i] = b[i];
        break;
    }
}
