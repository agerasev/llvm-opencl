float4 hy_dir_at(float4 src_pos, float4 src_dir, float4 dst_pos) {
    float4 p = src_pos, d = src_dir, h = dst_pos;
    return (float4)(
        h.z/p.z*d.x,
        h.z/p.z*d.y,
        d.z - length(p.xy - h.xy)/p.z*length(d.xy),
        0.0f
    );
}

__kernel void kernel_main(
    __global const float *a,
    __global const float *b,
    __global const float *c,
    __global float *d
) {
    int i = get_global_id(0);
    vstore4(hy_dir_at(
        vload4(i, a), vload4(i, b), vload4(i, b)
    ), i, d);
}
