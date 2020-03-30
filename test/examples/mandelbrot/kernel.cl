// Complex number multiplication
float2 cmul(float2 a, float2 b) {
    return (float2)(
        a.x*b.x - a.y*b.y,
        a.x*b.y + a.y*b.x
    );
}

// Sample one point
float sample(float2 c, uint depth) {
    float2 z = c;
    uint i;
    for (i = 0; i < depth; ++i) {
        z = cmul(z, z) + c;
        if (length(z) > (float)(1<<8)) {
            break;
        }
    }

    if (i < depth) {
        // Smooth step
        return (float)(i + 1) - log(log(length(z)))/log(2.0f);
    } else {
        return -1.0f;
    }
}

// Interpolate between colors
float3 interpolate(
    float n, __constant float *colors,
    uint color_count, uint color_width
) {
    float ip, fp;
    fp = modf(n/color_width, &ip);
    uint i = (uint)ip;
    float3 a = vload3(i % color_count, colors);
    float3 b = vload3((i + 1) % color_count, colors);
    return (1.0f - fp)*a + fp*b;
}


__kernel void render(
    __global float *image,
    __constant float *map,
    __constant float *colors,
    uint width, uint height,
    uint depth,
    uint ssf, // supersampling factor
    uint color_count,
    float color_width
) {
    // Get pixel coordinates
    uint x = get_global_id(0);
    uint y = get_global_id(1);

    float2 c = (float2)(
        ((float)x - (float)width/2),
        ((float)y - (float)height/2)
    )/height;
    float2 zoom = vload2(0, map);
    float2 shift = vload2(1, map);

    float2 sc;
    float3 color = (float3)(0.0f);
    // Iterate over supersampling points
    for (uint j = 0; j < ssf; ++j) {
        sc.y = c.y + (j + 0.5f)/(height*ssf);
        for (uint i = 0; i < ssf; ++i) {
            sc.x = c.x + (i + 0.5f)/(height*ssf);
            // Apply transformation and sample
            float n = sample(cmul(sc, zoom) - shift, depth);
            if (n > 0.0f) {
                // Get sample color
                color += interpolate(n, colors, color_count, color_width);
            }
        }
    }

    // Store pixel color
    color /= ssf*ssf;
    vstore3(color, y*width + x, image);
}
