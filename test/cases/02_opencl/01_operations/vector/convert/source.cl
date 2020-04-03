__kernel void kernel_main(
    __global int *uso, __global const uint *usi,
    __global uint *suo, __global const int *sui,
    __global float *ifo, __global const int *ifi,
    __global int *fio, __global const float *fii
) {
    int i = get_global_id(0);
    vstore4(convert_int4(  vload4(i, usi)), i, uso);
    vstore4(convert_uint4( vload4(i, sui)), i, suo);
    vstore4(convert_float4(vload4(i, ifi)), i, ifo);
    vstore4(convert_int4(  vload4(i, fii)), i, fio);
}
