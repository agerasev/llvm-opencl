float2 cmul(float2 a, float2 b) {
    return (float2)(
        a.x*b.x - a.y*b.y,
        a.x*b.y + a.y*b.x
    );
}

__kernel void kernel_main(
    __global float *img,
    uint w, uint h,
    uint depth
) {
    uint x = get_global_id(0);
    uint y = get_global_id(1);

    float2 c = (float2)(
        ((float)x - (float)(w-1)/2)/h,
        ((float)y - (float)(h-1)/2)/h
    );
    c = 2.0f*(c - (float2)(0.25f, 0.0f));
    float2 z = c;
    uint i;
    for (i = 0; i < depth; ++i) {
        z = cmul(z, z) + c;
        if (length(z) > 4.0f) {
            break;
        }
    }
    img[y*w + x] = (float)(i < depth);
}
